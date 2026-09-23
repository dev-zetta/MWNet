#include "Directory.hpp"
#include <components/openmw-mp/Security/TrustStore.hpp>
#include <sqlite3.h>
#include <boost/asio/ip/address.hpp>
#include <chrono>
#include <charconv>
#include <stdexcept>

namespace mwmp::discovery
{
    namespace
    {
        constexpr std::size_t maxStoredBytes = 16 * 1024 * 1024;
        void execute(sqlite3* db, const char* sql)
        {
            if (sqlite3_exec(db, sql, nullptr, nullptr, nullptr) != SQLITE_OK)
                throw std::runtime_error("directory database operation failed");
        }
        struct Statement
        {
            sqlite3_stmt* value = nullptr;
            Statement(sqlite3* db, const char* sql)
            {
                if (sqlite3_prepare_v2(db, sql, -1, &value, nullptr) != SQLITE_OK)
                    throw std::runtime_error("directory database statement failed");
            }
            ~Statement() { sqlite3_finalize(value); }
            void text(int index, const std::string& s)
            {
                if (sqlite3_bind_text(value, index, s.data(), static_cast<int>(s.size()), SQLITE_TRANSIENT) != SQLITE_OK)
                    throw std::runtime_error("directory database binding failed");
            }
            void integer(int index, std::int64_t n) { sqlite3_bind_int64(value, index, n); }
            void done()
            {
                if (sqlite3_step(value) != SQLITE_DONE) throw std::runtime_error("directory database write failed");
            }
        };
        Reply error(unsigned status, const char* message)
        {
            Json json; json.put("error", message); return {status, json};
        }
        std::string column(sqlite3_stmt* row, int index)
        {
            const auto* value = sqlite3_column_text(row, index);
            return value ? reinterpret_cast<const char*>(value) : "";
        }
        bool validId(std::string_view id)
        {
            return id.size() == 43 && id.find_first_not_of("0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ-_") == std::string_view::npos;
        }
    }
    Directory::Directory(std::string path, std::string origin, Clock clock, bool localTest)
        : mOrigin(std::move(origin)), mLocalTest(localTest), mClock(std::move(clock))
    {
        if (!validOrigin(mOrigin)) throw std::invalid_argument("invalid directory HTTPS origin");
        if (!mClock) mClock = [] { return std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count(); };
        if (sqlite3_open(path.c_str(), &mDb) != SQLITE_OK)
        {
            sqlite3_close(mDb); mDb = nullptr;
            throw std::runtime_error("cannot open directory database");
        }
        try
        {
            sqlite3_busy_timeout(mDb, 100);
            {
                Statement version(mDb, "PRAGMA user_version");
                if (sqlite3_step(version.value) != SQLITE_ROW || sqlite3_column_int(version.value,0) > 1)
                    throw std::runtime_error("unsupported directory database version");
            }
            sqlite3_limit(mDb, SQLITE_LIMIT_LENGTH, static_cast<int>(maximumBody));
            execute(mDb, "PRAGMA auto_vacuum=INCREMENTAL; PRAGMA journal_mode=WAL; PRAGMA synchronous=FULL;"
                "PRAGMA max_page_count=16384; PRAGMA cache_size=-2048; PRAGMA wal_autocheckpoint=128;"
                "CREATE TABLE IF NOT EXISTS listings(id TEXT PRIMARY KEY, envelope TEXT NOT NULL, expires INTEGER NOT NULL);"
                "CREATE TABLE IF NOT EXISTS blocks(kind TEXT NOT NULL, value TEXT NOT NULL, PRIMARY KEY(kind,value));"
                "PRAGMA user_version=1;");
            loadBlocks();
            Statement rows(mDb, "SELECT id,envelope,expires FROM listings ORDER BY id");
            std::size_t total = 0;
            while (sqlite3_step(rows.value) == SQLITE_ROW)
            {
                const auto id = column(rows.value, 0), json = column(rows.value, 1);
                total += json.size();
                if (mEntries.size() >= maximumListings || total > maxStoredBytes)
                    throw std::runtime_error("directory database exceeds listing limits");
                auto envelope = envelopeFromJson(parseJson(json));
                auto listing = verify(envelope, mOrigin);
                if (envelope.operation != "announce" || fingerprint(envelope).substr(8) != id)
                    throw std::runtime_error("invalid stored listing identity");
                Entry entry{envelope, listing};
                entry.expires = sqlite3_column_int64(rows.value, 2);
                entry.generation = ++mGeneration;
                mEntries.emplace(id, std::move(entry));
            }
            maintain();
        }
        catch (...) { sqlite3_close(mDb); mDb = nullptr; throw; }
    }
    Directory::~Directory() { sqlite3_close(mDb); }
    void Directory::store(const std::string& id, const Entry& entry)
    {
        Statement statement(mDb, "INSERT OR REPLACE INTO listings VALUES(?,?,?)");
        statement.text(1, id); statement.text(2, writeJson(toJson(entry.envelope)));
        statement.integer(3, entry.expires); statement.done();
    }
    void Directory::erase(const std::string& id)
    {
        Statement statement(mDb, "DELETE FROM listings WHERE id=?");
        statement.text(1,id); statement.done(); mEntries.erase(id);
    }
    void Directory::loadBlocks()
    {
        std::set<std::pair<std::string,std::string>> blocks;
        Statement rows(mDb, "SELECT kind,value FROM blocks LIMIT 10001");
        while (sqlite3_step(rows.value) == SQLITE_ROW)
            blocks.emplace(column(rows.value,0), column(rows.value,1));
        if (blocks.size() > 10000) throw std::runtime_error("too many directory blocks");
        mBlocks = std::move(blocks);
    }
    bool Directory::blocked(std::string_view id, const Listing& listing) const
    {
        return mBlocks.contains({"identity", std::string(id)}) || mBlocks.contains({"endpoint",
            *security::TrustStore::canonicalEndpoint(
                boost::asio::ip::make_address(listing.host).to_string(), listing.port)});
    }
    void Directory::block(std::string kind, std::string value, bool remove)
    {
        if ((kind != "identity" && kind != "endpoint") || value.empty() || value.size() > 300
            || (kind == "identity" && !validId(value))) throw std::invalid_argument("invalid block");
        if (kind == "endpoint")
        {
            const auto split = value.rfind(':');
            if (split == std::string::npos) throw std::invalid_argument("endpoint block requires an IP and port");
            auto host = value.substr(0,split), port = value.substr(split+1);
            if (host.size() > 2 && host.front() == '[' && host.back() == ']') host = host.substr(1,host.size()-2);
            unsigned parsedPort = 0;
            auto parsed = std::from_chars(port.data(),port.data()+port.size(),parsedPort);
            if (parsed.ec != std::errc{} || parsed.ptr != port.data()+port.size() || parsedPort == 0 || parsedPort > 65535)
                throw std::invalid_argument("invalid blocked port");
            value = *security::TrustStore::canonicalEndpoint(boost::asio::ip::make_address(host).to_string(),
                static_cast<std::uint16_t>(parsedPort));
        }
        loadBlocks();
        if (!remove && mBlocks.size() >= 10000) throw std::runtime_error("block limit reached");
        Statement s(mDb, remove ? "DELETE FROM blocks WHERE kind=? AND value=?" : "INSERT OR IGNORE INTO blocks VALUES(?,?)");
        s.text(1,kind); s.text(2,value); s.done(); mNextMaintenance = 0; maintain();
    }
    void Directory::backup(const std::string& path)
    {
        sqlite3* destination = nullptr;
        if (sqlite3_open(path.c_str(), &destination) != SQLITE_OK)
        { sqlite3_close(destination); throw std::runtime_error("cannot open backup destination"); }
        auto* backup = sqlite3_backup_init(destination, "main", mDb, "main");
        int status = backup ? sqlite3_backup_step(backup, -1) : SQLITE_ERROR;
        if (backup) sqlite3_backup_finish(backup);
        sqlite3_close(destination);
        if (status != SQLITE_DONE) throw std::runtime_error("directory backup failed");
    }
    void Directory::maintain()
    {
        const auto now = mClock();
        if (now < mNextMaintenance) return;
        mNextMaintenance = now + 1;
        loadBlocks();
        for (auto it = mEntries.begin(); it != mEntries.end(); )
        {
            if (it->second.expires <= now || blocked(it->first, it->second.listing))
            { const auto id = it->first; ++it; erase(id); }
            else ++it;
        }
        std::erase_if(mChallenges, [now](const auto& p) { return p.second.expires <= now; });
        std::erase_if(mRates, [now](const auto& p) { return p.second.start + 60 <= now; });
        execute(mDb, "PRAGMA incremental_vacuum(16)");
    }
    Reply Directory::handle(std::string_view method, std::string_view target, std::string_view body,
        const std::string& source)
    {
        maintain();
        if (body.size() > maximumBody || target.size() > 256) return error(413, "request too large");
        auto rate = mRates.find(source);
        if (rate == mRates.end())
        {
            if (mRates.size() >= 4096) return error(429, "rate table full");
            rate = mRates.emplace(source, Rate{mClock(),0}).first;
        }
        // Includes reads and failed submissions, including those behind the proxy.
        if (++rate->second.count > 600) return error(429, "request rate exceeded");
        try
        {
            if (method == "GET" && (target == "/v1/servers" || target.starts_with("/v1/servers?after=")))
            {
                const std::string after(target == "/v1/servers" ? "" : target.substr(18));
                if (!after.empty() && !validId(after)) return error(400,"invalid cursor");
                Json response, servers;
                std::string last;
                std::size_t count = 0, bytes = 0;
                auto it = mEntries.upper_bound(after);
                for (; it != mEntries.end(); ++it)
                {
                    const auto& entry = it->second;
                    if (!entry.verified || entry.expires <= mClock()) continue;
                    auto object = toJson(entry.envelope);
                    const auto size = writeJson(object).size();
                    if (count == 100 || bytes + size > maximumBody - 1024) break;
                    servers.push_back({"", object}); last = it->first; bytes += size; ++count;
                }
                response.add_child("servers",servers);
                response.put("next", it == mEntries.end() ? "" : last);
                return {200,response};
            }
            if (method == "POST" && target == "/v1/challenges")
            {
                auto json = parseJson(body);
                requireFields(json,{"publicKey","operation"});
                auto key = scalar(json,"publicKey",64), operation = scalar(json,"operation",8);
                if (unhex(key,32).size() != 32 || (operation != "announce" && operation != "withdraw"))
                    return error(400,"invalid challenge request");
                Envelope e; e.publicKey = key;
                if (mBlocks.contains({"identity",fingerprint(e).substr(8)})) return error(403,"identity blocked");
                std::size_t perKey = 0;
                for (const auto& [nonce,c] : mChallenges) { (void)nonce; if (c.key == key && c.source == source) ++perKey; }
                if (mChallenges.size() >= 2048 || perKey >= 2) return error(429,"challenge limit exceeded");
                auto nonce = randomNonce();
                mChallenges.emplace(nonce, Challenge{key,operation,source,mClock()+30});
                Json result; result.put("nonce",nonce); return {200,result};
            }
            if ((method == "PUT" || method == "DELETE") && target.starts_with("/v1/servers/"))
            {
                const std::string id(target.substr(12));
                if (!validId(id)) return error(400,"invalid server id");
                auto envelope = envelopeFromJson(parseJson(body));
                auto listing = verify(envelope,mOrigin);
                boost::system::error_code addressError;
                auto address = boost::asio::ip::make_address(listing.host,addressError);
                if (addressError || address.is_unspecified() || address.is_multicast()
                    || (!mLocalTest && !publicAddress(listing.host)))
                    return error(400,"a permitted numeric IP address is required");
                if (fingerprint(envelope).substr(8) != id || envelope.operation != (method == "PUT" ? "announce" : "withdraw"))
                    return error(400,"operation or identity mismatch");
                auto nonce = mChallenges.find(envelope.nonce);
                if (nonce == mChallenges.end() || nonce->second.expires <= mClock()
                    || nonce->second.key != envelope.publicKey || nonce->second.operation != envelope.operation
                    || nonce->second.source != source)
                    return error(409,"expired or consumed challenge");
                mChallenges.erase(nonce);
                if (blocked(id,listing)) return error(403,"server blocked");
                if (method == "DELETE") { erase(id); return {200,{}}; }
                auto previous = mEntries.find(id);
                if (previous == mEntries.end() && mEntries.size() >= maximumListings) return error(429,"directory full");
                std::size_t bytes = envelope.payload.size() + 1024;
                for (const auto& [key,e] : mEntries) if (key != id) bytes += e.envelope.payload.size() + 1024;
                if (bytes > maxStoredBytes) return error(429,"directory byte budget exceeded");
                Entry entry{envelope,listing};
                entry.expires = mClock()+180;
                if (previous != mEntries.end() && previous->second.listing.host == listing.host
                    && previous->second.listing.port == listing.port)
                {
                    entry.checked = previous->second.checked; entry.retry = previous->second.retry;
                    entry.verified = previous->second.verified; entry.pending = previous->second.pending;
                    entry.generation = previous->second.generation;
                }
                else entry.generation = ++mGeneration;
                store(id,entry);
                mEntries.insert_or_assign(id,entry);
                return {entry.verified ? 200U : 202U,{}};
            }
            return error(404,"unknown directory endpoint");
        }
        catch (const std::invalid_argument&) { return error(400,"invalid discovery data"); }
        catch (const boost::property_tree::ptree_error&) { return error(400,"invalid discovery JSON"); }
    }
    std::optional<Verification> Directory::nextVerification()
    {
        maintain();
        auto selected = mEntries.end();
        unsigned pending = 0;
        for (auto it = mEntries.begin(); it != mEntries.end(); ++it)
        {
            const auto& e = it->second;
            pending += e.pending;
            if (e.pending || e.retry > mClock() || (e.verified && e.checked+300 > mClock())) continue;
            if (selected == mEntries.end() || e.checked < selected->second.checked)
                selected = it;
        }
        if (pending >= 2 || selected == mEntries.end()) return {};
        selected->second.pending = true;
        return Verification{selected->first,selected->second.listing,selected->second.generation};
    }
    void Directory::complete(const Verification& job, bool reachable)
    {
        auto found = mEntries.find(job.id);
        if (found == mEntries.end() || found->second.generation != job.generation) return;
        auto& entry = found->second;
        entry.pending = false; entry.verified = reachable;
        entry.checked = mClock(); entry.retry = mClock()+30;
        if (!reachable) ++mFailures;
    }
    Json Directory::metrics() const
    {
        Json json;
        std::size_t visible = 0, pending = 0, bytes = 0;
        for (const auto& [id,e] : mEntries)
        { (void)id; visible += e.verified; pending += e.pending; bytes += e.envelope.payload.size()+1024; }
        json.put("listings",mEntries.size()); json.put("visible",visible); json.put("pending",pending);
        json.put("challenges",mChallenges.size()); json.put("rateBuckets",mRates.size());
        json.put("listingBytes",bytes); json.put("verificationFailures",mFailures);
        return json;
    }
}
