#ifndef OPENMW_MP_TRANSPORT_SNAPSHOT_SEQUENCE_TRACKER_HPP
#define OPENMW_MP_TRANSPORT_SNAPSHOT_SEQUENCE_TRACKER_HPP

#include "ITransport.hpp"

#include <cstddef>
#include <cstdint>
#include <unordered_map>

namespace mwmp::transport
{
    class SnapshotSequenceTracker
    {
    public:
        explicit SnapshotSequenceTracker(std::size_t maximumSubjects = 4096);

        bool accept(TransportConnectionId connection, MessageLane lane, std::uint64_t subject,
            std::uint64_t sequence);
        void erase(TransportConnectionId connection);
        void clear();
        std::size_t size() const noexcept;

    private:
        struct Key
        {
            TransportConnectionId connection;
            MessageLane lane = MessageLane::System;
            std::uint64_t subject = 0;

            bool operator==(const Key&) const = default;
        };

        struct KeyHash
        {
            std::size_t operator()(const Key& key) const noexcept;
        };

        const std::size_t mMaximumSubjects;
        std::unordered_map<Key, std::uint64_t, KeyHash> mSequences;
    };
}

#endif
