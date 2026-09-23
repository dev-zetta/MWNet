#ifndef TES3MP_DISCOVERY_CLIENT_HPP
#define TES3MP_DISCOVERY_CLIENT_HPP
#include "Protocol.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace mwmp::discovery
{
    struct HttpOptions
    {
        std::string origin;
        std::string caFile;
    };
    struct HttpResult { unsigned status = 0; std::string body; };
    HttpResult request(const HttpOptions& options, std::string_view method, std::string_view path,
        std::string_view body, const std::atomic_bool& cancel);
    struct Server { Listing listing; std::string fingerprint; };
    struct Page { std::vector<Server> servers; std::string next; };
    Page fetch(const HttpOptions& options, std::string_view cursor, const std::atomic_bool& cancel);
    std::string challenge(const HttpOptions& options, std::string_view publicKey,
        std::string_view operation, const std::atomic_bool& cancel);
    unsigned publish(const HttpOptions& options, const Envelope& envelope, const std::atomic_bool& cancel);

    class Announcer
    {
    public:
        Announcer(HttpOptions options, security::ServerIdentity identity, Listing listing);
        ~Announcer();
        Announcer(const Announcer&) = delete;
        Announcer& operator=(const Announcer&) = delete;
        void update(Listing listing);
        std::string status() const;
    private:
        void run();
        HttpOptions mOptions;
        security::ServerIdentity mIdentity;
        mutable std::mutex mMutex;
        std::condition_variable mWake;
        Listing mListing;
        std::string mStatus;
        std::atomic_bool mStop{false};
        std::thread mThread;
    };
}
#endif
