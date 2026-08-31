#ifndef OPENMW_MP_PERSISTENCE_PERSISTENCE_SERVICE_HPP
#define OPENMW_MP_PERSISTENCE_PERSISTENCE_SERVICE_HPP

#include "AtomicFile.hpp"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace mwmp::persistence
{
    enum class QueueDecision
    {
        Queued,
        Coalesced,
        InvalidRequest,
        QueueFull,
        Stopping,
    };

    struct PersistenceResult
    {
        std::filesystem::path path;
        bool success = false;
        std::string error;
    };

    class PersistenceService
    {
    public:
        using Completion = std::function<void(const PersistenceResult&)>;
        using Writer = std::function<bool(const std::filesystem::path&,
            std::span<const std::byte>, const AtomicWriteOptions&, std::string&)>;

        explicit PersistenceService(std::size_t maximumPendingWrites = 256,
            Writer writer = {});
        ~PersistenceService();

        PersistenceService(const PersistenceService&) = delete;
        PersistenceService& operator=(const PersistenceService&) = delete;

        QueueDecision save(std::filesystem::path path,
            std::span<const std::byte> contents, AtomicWriteOptions options = {},
            Completion completion = {});
        void flush();
        void stop() noexcept;

        std::size_t pending() const noexcept;
        bool stopping() const noexcept;

    private:
        struct Request
        {
            std::filesystem::path path;
            std::string key;
            std::vector<std::byte> contents;
            AtomicWriteOptions options;
            std::vector<Completion> completions;
        };

        void run(std::stop_token stopToken) noexcept;

        std::size_t mMaximumPendingWrites;
        Writer mWriter;
        mutable std::mutex mMutex;
        std::condition_variable mWorkAvailable;
        std::condition_variable mIdle;
        std::deque<Request> mRequests;
        bool mWriting = false;
        std::atomic_bool mStopping = false;
        std::jthread mWorker;
    };

    const char* describe(QueueDecision decision) noexcept;
}

#endif
