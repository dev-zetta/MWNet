#include "SnapshotSequenceTracker.hpp"

#include <functional>

namespace mwmp::transport
{
    SnapshotSequenceTracker::SnapshotSequenceTracker(std::size_t maximumSubjects)
        : mMaximumSubjects(maximumSubjects)
    {
    }

    bool SnapshotSequenceTracker::accept(TransportConnectionId connection, MessageLane lane,
        std::uint64_t subject, std::uint64_t sequence)
    {
        if (!connection)
            return false;

        const Key key{ connection, lane, subject };
        const auto found = mSequences.find(key);
        if (found != mSequences.end())
        {
            if (sequence <= found->second)
                return false;
            found->second = sequence;
            return true;
        }

        if (mSequences.size() >= mMaximumSubjects)
            return false;
        mSequences.emplace(key, sequence);
        return true;
    }

    void SnapshotSequenceTracker::erase(TransportConnectionId connection)
    {
        for (auto it = mSequences.begin(); it != mSequences.end();)
        {
            if (it->first.connection == connection)
                it = mSequences.erase(it);
            else
                ++it;
        }
    }

    void SnapshotSequenceTracker::clear()
    {
        mSequences.clear();
    }

    std::size_t SnapshotSequenceTracker::size() const noexcept
    {
        return mSequences.size();
    }

    std::size_t SnapshotSequenceTracker::KeyHash::operator()(const Key& key) const noexcept
    {
        const std::size_t connection = std::hash<std::uint64_t>{}(key.connection.value);
        const std::size_t subject = std::hash<std::uint64_t>{}(key.subject);
        const std::size_t lane = std::hash<std::uint8_t>{}(static_cast<std::uint8_t>(key.lane));
        return connection ^ (subject + 0x9e3779b9U + (connection << 6U) + (connection >> 2U))
            ^ (lane << 1U);
    }
}
