#include "Client.hpp"
#include <curl/curl.h>
#include <algorithm>
#include <memory>
#include <stdexcept>

namespace mwmp::discovery
{
    namespace
    {
        struct CurlGlobal
        {
            CurlGlobal()
            {
                if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
                    throw std::runtime_error("HTTPS initialization failed");
            }
            ~CurlGlobal() { curl_global_cleanup(); }
        };
        std::size_t receive(char* data, std::size_t size, std::size_t count, void* pointer)
        {
            auto& out = *static_cast<std::string*>(pointer);
            if (size != 1 || count > maximumBody - out.size()) return 0;
            try { out.append(data, count); return count; } catch (...) { return 0; }
        }
        int progress(void* pointer, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
        {
            return static_cast<const std::atomic_bool*>(pointer)->load() ? 1 : 0;
        }
    }
    HttpResult request(const HttpOptions& o, std::string_view method, std::string_view path,
        std::string_view body, const std::atomic_bool& cancel)
    {
        if (!validOrigin(o.origin) || !path.starts_with("/v1/") || body.size() > maximumBody)
            throw std::invalid_argument("invalid directory HTTPS request");
        static CurlGlobal global;
        (void)global;
        std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), curl_easy_cleanup);
        if (!curl) throw std::runtime_error("HTTPS allocation failed");
        const std::string url = o.origin + std::string(path), verb(method);
        HttpResult result;
        auto* headers = curl_slist_append(nullptr, "Content-Type: application/json");
        if (!headers) throw std::runtime_error("HTTPS header allocation failed");
        std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> ownedHeaders(headers, curl_slist_free_all);
        curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl.get(), CURLOPT_CUSTOMREQUEST, verb.c_str());
        curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl.get(), CURLOPT_PROTOCOLS_STR, "https");
        curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 0L);
        curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYHOST, 2L);
        curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT_MS, 2000L);
        curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT_MS, 5000L);
        curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl.get(), CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl.get(), CURLOPT_XFERINFOFUNCTION, progress);
        curl_easy_setopt(curl.get(), CURLOPT_XFERINFODATA, &cancel);
        curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, receive);
        curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &result.body);
        if (!o.caFile.empty()) curl_easy_setopt(curl.get(), CURLOPT_CAINFO, o.caFile.c_str());
        if (method != "GET")
        {
            curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, body.data());
            curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(body.size()));
        }
        const auto code = curl_easy_perform(curl.get());
        if (code != CURLE_OK) throw std::runtime_error(std::string("Directory request: ") + curl_easy_strerror(code));
        long status = 0;
        curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status);
        result.status = static_cast<unsigned>(status);
        return result;
    }
    Page fetch(const HttpOptions& options, std::string_view cursor, const std::atomic_bool& cancel)
    {
        if (cursor.size() > 64 || !std::all_of(cursor.begin(), cursor.end(), [](char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z')
                || (c >= 'A' && c <= 'Z') || c == '-' || c == '_'; }))
            throw std::invalid_argument("invalid directory cursor");
        auto response = request(options, "GET", "/v1/servers?after=" + std::string(cursor), {}, cancel);
        if (response.status != 200) throw std::runtime_error("Directory unavailable (HTTP " + std::to_string(response.status) + ")");
        auto json = parseJson(response.body);
        requireFields(json, {"servers", "next"});
        Page page;
        page.next = scalar(json, "next", 64);
        if (!json.get_child("servers").data().empty()) throw std::runtime_error("invalid server list");
        std::string previous(cursor);
        for (const auto& [index, item] : json.get_child("servers"))
        {
            if (!index.empty() || page.servers.size() >= 100) throw std::runtime_error("invalid directory page");
            auto envelope = envelopeFromJson(item);
            if (envelope.operation != "announce") throw std::runtime_error("invalid listed operation");
            const auto id = fingerprint(envelope).substr(8);
            if (id <= previous) throw std::runtime_error("unordered or duplicate directory identity");
            previous = id;
            page.servers.push_back({ verify(envelope, options.origin), fingerprint(envelope) });
        }
        if (!page.next.empty() && (page.servers.empty() || page.next != previous))
            throw std::runtime_error("invalid directory continuation");
        return page;
    }
    std::string challenge(const HttpOptions& options, std::string_view publicKey,
        std::string_view operation, const std::atomic_bool& cancel)
    {
        Json body;
        body.put("publicKey", publicKey); body.put("operation", operation);
        auto response = request(options, "POST", "/v1/challenges", writeJson(body), cancel);
        if (response.status != 200) throw std::runtime_error("Directory challenge rejected (HTTP " + std::to_string(response.status) + ")");
        auto json = parseJson(response.body);
        requireFields(json, {"nonce"});
        auto nonce = scalar(json, "nonce", 64);
        if (unhex(nonce, 32).size() != 32) throw std::runtime_error("invalid directory challenge");
        return nonce;
    }
    unsigned publish(const HttpOptions& options, const Envelope& envelope, const std::atomic_bool& cancel)
    {
        auto response = request(options, envelope.operation == "withdraw" ? "DELETE" : "PUT",
            "/v1/servers/" + fingerprint(envelope).substr(8), writeJson(toJson(envelope)), cancel);
        if (response.status != 200 && response.status != 202)
            throw std::runtime_error("Directory announcement rejected (HTTP " + std::to_string(response.status) + ")");
        return response.status;
    }
    Announcer::Announcer(HttpOptions options, security::ServerIdentity identity, Listing listing)
        : mOptions(std::move(options)), mIdentity(std::move(identity)), mListing(std::move(listing))
    {
        if (!validOrigin(mOptions.origin)) throw std::invalid_argument("invalid directory origin");
        (void)encode(mListing);
        mThread = std::thread([this] { run(); });
    }
    Announcer::~Announcer()
    {
        mStop = true;
        mWake.notify_all();
        if (mThread.joinable()) mThread.join();
        // No blocking withdrawal on shutdown. The last accepted lease expires in 180 seconds.
    }
    void Announcer::update(Listing listing)
    {
        std::lock_guard lock(mMutex);
        mListing = std::move(listing);
    }
    std::string Announcer::status() const
    {
        std::lock_guard lock(mMutex);
        return mStatus;
    }
    void Announcer::run()
    {
        unsigned delay = 5;
        while (!mStop)
        {
            Listing listing;
            { std::lock_guard lock(mMutex); listing = mListing; }
            unsigned wait = delay;
            try
            {
                auto nonce = challenge(mOptions, hex(mIdentity.publicKey()), "announce", mStop);
                auto status = publish(mOptions, sign(mIdentity, listing, mOptions.origin, "announce", nonce), mStop);
                { std::lock_guard lock(mMutex); mStatus = status == 200 ? "listed" : "verification pending"; }
                delay = 5;
                wait = 60;
            }
            catch (const std::exception& e)
            {
                { std::lock_guard lock(mMutex); mStatus = e.what(); }
                delay = std::min(delay * 2, 60U);
            }
            std::unique_lock lock(mMutex);
            mWake.wait_for(lock, std::chrono::seconds(wait), [this] { return mStop.load(); });
        }
    }
}
