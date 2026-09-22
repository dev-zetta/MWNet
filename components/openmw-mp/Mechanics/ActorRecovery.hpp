#ifndef OPENMW_MP_ACTOR_RECOVERY_HPP
#define OPENMW_MP_ACTOR_RECOVERY_HPP

#include "ActorStateLedger.hpp"
#include "MovementProgress.hpp"

namespace mwmp::mechanics
{
    struct ActorRecoveryAnchor
    {
        ActorIdentity identity;
        std::string refId;
        ActorTransform transform;
    };

    class ActorRecovery
    {
    public:
        using Clock = ActorStateLedger::Clock;
        static constexpr double MaximumRecoveryDistance = 4096;
        static constexpr double AcknowledgementRadius = 96;
        static constexpr unsigned MaximumRecoveries = 3;

        bool install(const std::vector<ActorRecoveryAnchor>& anchors)
        {
            if (anchors.size() > ActorStateLedger::DefaultMaximumActors)
                return false;
            decltype(mAnchors) prepared;
            for (const auto& anchor : anchors)
            {
                const auto valid = [](Position3 p) {
                    const auto coordinate = [](double value) {
                        return std::isfinite(value) && std::abs(value) <= MovementValidator::MaximumCoordinateMagnitude;
                    };
                    return coordinate(p.x) && coordinate(p.y) && coordinate(p.z);
                };
                if (anchor.identity.cell.empty() || !anchor.identity.refNum || anchor.identity.mpNum
                    || anchor.refId.empty() || !valid(anchor.transform.position) || !valid(anchor.transform.rotation)
                    || !prepared.emplace(anchor.identity, anchor).second)
                    return false;
            }
            mAnchors.swap(prepared);
            mObservations.clear();
            return true;
        }

        std::optional<ActorTransform> observe(const ActorIdentity& id, std::string_view refId,
            Position3 position, bool tryingToMove, std::uint64_t lease, Clock::time_point now)
        {
            const auto anchor = mAnchors.find(id);
            if (anchor == mAnchors.end() || anchor->second.refId != refId || !lease)
                return std::nullopt;
            auto& state = mObservations[id];
            const auto elapsed = std::chrono::duration<double>(now - state.lastSample).count();
            if (state.lease != lease || elapsed < 0 || elapsed > 3)
                state.progress.reset();
            if (state.lease != lease)
                state.pending = false;
            state.lease = lease;
            state.lastSample = now;
            if (state.pending)
            {
                if (now >= state.pendingUntil
                    || MovementProgress::distance(position, anchor->second.transform.position) <= AcknowledgementRadius)
                    state.pending = false;
                else
                    return std::nullopt;
            }
            if (now < state.cooldown || state.recoveries >= MaximumRecoveries)
                return std::nullopt;
            if (!state.progress.update(position, tryingToMove, elapsed >= 0 && elapsed <= 3 ? elapsed : 0))
                return std::nullopt;
            state.progress.reset();
            state.cooldown = now + std::chrono::seconds(120);
            const double distance = MovementProgress::distance(position, anchor->second.transform.position);
            if (distance < 8 || distance > MaximumRecoveryDistance)
                return std::nullopt;
            ++state.recoveries;
            state.pending = true;
            state.pendingUntil = now + std::chrono::seconds(5);
            return anchor->second.transform;
        }

        // Pre-correction unreliable snapshots may still be in flight. Ignore and
        // resend the fixed destination briefly; never grant arbitrary movement.
        std::optional<ActorTransform> pending(const ActorIdentity& id, Position3 position,
            std::uint64_t lease, Clock::time_point now) const
        {
            const auto found = mObservations.find(id);
            const auto anchor = mAnchors.find(id);
            if (found == mObservations.end() || anchor == mAnchors.end())
                return std::nullopt;
            const auto& state = found->second;
            if (state.pending && state.lease == lease && now < state.pendingUntil
                && MovementProgress::distance(position, anchor->second.transform.position) > AcknowledgementRadius)
                return anchor->second.transform;
            return std::nullopt;
        }
        void forget(const ActorIdentity& id) { mObservations.erase(id); }
        std::size_t size() const { return mObservations.size(); }
    private:
        struct Observation
        {
            MovementProgress progress;
            Clock::time_point lastSample{};
            Clock::time_point cooldown{};
            Clock::time_point pendingUntil{};
            std::uint64_t lease = 0;
            unsigned recoveries = 0;
            bool pending = false;
        };
        std::unordered_map<ActorIdentity, ActorRecoveryAnchor, ActorIdentityHash> mAnchors;
        std::unordered_map<ActorIdentity, Observation, ActorIdentityHash> mObservations;
    };
}
#endif
