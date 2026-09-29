#ifndef MWNET_DISCOVERY_DIRECTORY_HPP
#define MWNET_DISCOVERY_DIRECTORY_HPP
#include "Protocol.hpp"
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>

struct sqlite3;
namespace mwmp::discovery
{
    struct Reply { unsigned status; Json body; };
    struct Verification { std::string id; Listing listing; std::uint64_t generation; };
    class Directory
    {
    public:
        using Clock = std::function<std::int64_t()>;
        Directory(std::string database, std::string origin, Clock clock = {}, bool localTest = false);
        ~Directory();
        Directory(const Directory&) = delete;
        Directory& operator=(const Directory&) = delete;
        Reply handle(std::string_view method, std::string_view target, std::string_view body,
            const std::string& source);
        void maintain();
        std::optional<Verification> nextVerification();
        void complete(const Verification& job, bool reachable);
        Json metrics() const;
        void block(std::string kind, std::string value, bool remove = false);
        void backup(const std::string& destination);
    private:
        struct Entry
        {
            Envelope envelope;
            Listing listing;
            std::int64_t expires = 0, checked = 0, retry = 0;
            std::uint64_t generation = 0;
            bool verified = false, pending = false;
        };
        struct Challenge { std::string key, operation, source; std::int64_t expires; };
        struct Rate { std::int64_t start; unsigned count; };
        bool blocked(std::string_view id, const Listing& listing) const;
        void store(const std::string& id, const Entry& entry);
        void erase(const std::string& id);
        void loadBlocks();
        sqlite3* mDb = nullptr;
        std::string mOrigin;
        bool mLocalTest = false;
        Clock mClock;
        std::map<std::string, Entry> mEntries;
        std::map<std::string, Challenge> mChallenges;
        std::map<std::string, Rate> mRates;
        std::set<std::pair<std::string, std::string>> mBlocks;
        std::uint64_t mGeneration = 0, mFailures = 0;
        std::int64_t mNextMaintenance = 0;
    };
}
#endif
