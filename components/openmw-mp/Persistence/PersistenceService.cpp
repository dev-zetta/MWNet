#include "PersistenceService.hpp"

#include <exception>
#include <utility>

namespace mwmp::persistence
{
    PersistenceService::PersistenceService(std::size_t maximumPendingWrites, Writer writer)
        : mMaximumPendingWrites(maximumPendingWrites)
        , mWriter(std::move(writer))
    {
        if (!mWriter)
            mWriter = writeFileAtomically;
        mWorker = std::jthread([this](std::stop_token stopToken) { run(stopToken); });
    }

    PersistenceService::~PersistenceService()
    {
        stop();
    }

    QueueDecision PersistenceService::save(std::filesystem::path path,
        std::span<const std::byte> contents, AtomicWriteOptions options,
        Completion completion)
    {
        if (path.empty() || contents.size() > options.maximumBytes)
            return QueueDecision::InvalidRequest;
        if (mStopping.load(std::memory_order_acquire))
            return QueueDecision::Stopping;

        Request request;
        request.path = std::move(path);
        request.key = request.path.lexically_normal().generic_string();
        request.contents.assign(contents.begin(), contents.end());
        request.options = std::move(options);
        if (completion)
            request.completions.push_back(std::move(completion));

        std::lock_guard lock(mMutex);
        if (mStopping.load(std::memory_order_relaxed))
            return QueueDecision::Stopping;
        for (Request& pending : mRequests)
        {
            if (pending.key != request.key)
                continue;
            for (Completion& pendingCompletion : request.completions)
                pending.completions.push_back(std::move(pendingCompletion));
            request.completions = std::move(pending.completions);
            pending = std::move(request);
            return QueueDecision::Coalesced;
        }
        if (mMaximumPendingWrites == 0 || mRequests.size() >= mMaximumPendingWrites)
            return QueueDecision::QueueFull;
        mRequests.push_back(std::move(request));
        mWorkAvailable.notify_one();
        return QueueDecision::Queued;
    }

    void PersistenceService::flush()
    {
        std::unique_lock lock(mMutex);
        mIdle.wait(lock, [this] { return mRequests.empty() && !mWriting; });
    }

    void PersistenceService::stop() noexcept
    {
        if (mStopping.exchange(true, std::memory_order_acq_rel))
            return;
        mWorker.request_stop();
        mWorkAvailable.notify_all();
        if (mWorker.joinable())
            mWorker.join();
    }

    std::size_t PersistenceService::pending() const noexcept
    {
        std::lock_guard lock(mMutex);
        return mRequests.size();
    }

    bool PersistenceService::stopping() const noexcept
    {
        return mStopping.load(std::memory_order_acquire);
    }

    void PersistenceService::run(std::stop_token stopToken) noexcept
    {
        while (true)
        {
            Request request;
            {
                std::unique_lock lock(mMutex);
                mWorkAvailable.wait(lock,
                    [this, &stopToken] { return stopToken.stop_requested() || !mRequests.empty(); });
                if (mRequests.empty())
                {
                    if (stopToken.stop_requested())
                        break;
                    continue;
                }
                request = std::move(mRequests.front());
                mRequests.pop_front();
                mWriting = true;
            }

            PersistenceResult result;
            result.path = request.path;
            try
            {
                result.success = mWriter(request.path, request.contents,
                    request.options, result.error);
            }
            catch (const std::exception& exception)
            {
                result.error = exception.what();
            }
            catch (...)
            {
                result.error = "persistence writer threw an unknown exception";
            }

            for (const Completion& completion : request.completions)
            {
                try
                {
                    completion(result);
                }
                catch (...)
                {
                }
            }

            {
                std::lock_guard lock(mMutex);
                mWriting = false;
                if (mRequests.empty())
                    mIdle.notify_all();
            }
        }

        std::lock_guard lock(mMutex);
        mWriting = false;
        mIdle.notify_all();
    }

    const char* describe(QueueDecision decision) noexcept
    {
        switch (decision)
        {
            case QueueDecision::Queued:
                return "the persistence write was queued";
            case QueueDecision::Coalesced:
                return "the persistence write replaced an older pending write";
            case QueueDecision::InvalidRequest:
                return "the persistence request is invalid";
            case QueueDecision::QueueFull:
                return "the persistence queue is full";
            case QueueDecision::Stopping:
                return "the persistence service is stopping";
        }
        return "unknown persistence queue decision";
    }
}
