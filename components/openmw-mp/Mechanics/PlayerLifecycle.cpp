#include "PlayerLifecycle.hpp"

namespace mwmp::mechanics
{
    PlayerLifecycle::PlayerLifecycle(std::size_t maximumPlayers)
        : mMaximumPlayers(maximumPlayers)
    {
    }

    PlayerLifeTransition PlayerLifecycle::reportDeath(std::uint64_t connection)
    {
        if (connection == 0)
            return { PlayerLifeDecision::InvalidConnection };
        State* current = getOrCreate(connection);
        if (current == nullptr)
            return { PlayerLifeDecision::CapacityReached };
        if (current->value != PlayerLifeState::Alive)
            return { PlayerLifeDecision::AlreadyDead, current->value, current->generation };
        current->value = PlayerLifeState::Dead;
        current->respawnType = 0;
        ++current->generation;
        return { PlayerLifeDecision::Applied, current->value, current->generation };
    }

    PlayerLifeTransition PlayerLifecycle::beginRespawn(
        std::uint64_t connection, std::uint32_t respawnType)
    {
        if (connection == 0)
            return { PlayerLifeDecision::InvalidConnection };
        if (respawnType > MaximumRespawnType)
            return { PlayerLifeDecision::InvalidRespawnType, state(connection) };
        State* current = getOrCreate(connection);
        if (current == nullptr)
            return { PlayerLifeDecision::CapacityReached };
        if (current->value != PlayerLifeState::Dead)
            return { PlayerLifeDecision::NotDead, current->value, current->generation };
        current->value = PlayerLifeState::Respawning;
        current->respawnType = respawnType;
        return { PlayerLifeDecision::Applied, current->value, current->generation };
    }

    PlayerLifeTransition PlayerLifecycle::acknowledgeRespawn(
        std::uint64_t connection, std::uint32_t respawnType)
    {
        if (connection == 0)
            return { PlayerLifeDecision::InvalidConnection };
        const auto found = mStates.find(connection);
        if (found == mStates.end() || found->second.value != PlayerLifeState::Respawning)
            return { PlayerLifeDecision::NotRespawning, state(connection) };
        if (respawnType != found->second.respawnType)
            return { PlayerLifeDecision::RespawnTypeMismatch,
                found->second.value, found->second.generation };
        found->second.value = PlayerLifeState::Alive;
        found->second.respawnType = 0;
        return { PlayerLifeDecision::Applied,
            found->second.value, found->second.generation };
    }

    PlayerLifeState PlayerLifecycle::state(std::uint64_t connection) const noexcept
    {
        const auto found = mStates.find(connection);
        return found == mStates.end() ? PlayerLifeState::Alive : found->second.value;
    }

    std::optional<std::uint32_t> PlayerLifecycle::pendingRespawnType(
        std::uint64_t connection) const noexcept
    {
        const auto found = mStates.find(connection);
        if (found == mStates.end() || found->second.value != PlayerLifeState::Respawning)
            return std::nullopt;
        return found->second.respawnType;
    }

    bool PlayerLifecycle::erase(std::uint64_t connection) noexcept
    {
        return mStates.erase(connection) != 0;
    }

    void PlayerLifecycle::clear() noexcept
    {
        mStates.clear();
    }

    std::size_t PlayerLifecycle::size() const noexcept
    {
        return mStates.size();
    }

    PlayerLifecycle::State* PlayerLifecycle::getOrCreate(std::uint64_t connection)
    {
        const auto found = mStates.find(connection);
        if (found != mStates.end())
            return &found->second;
        if (mStates.size() >= mMaximumPlayers)
            return nullptr;
        return &mStates.emplace(connection, State{}).first->second;
    }

    const char* describe(PlayerLifeDecision decision) noexcept
    {
        switch (decision)
        {
            case PlayerLifeDecision::Applied:
                return "lifecycle transition applied";
            case PlayerLifeDecision::InvalidConnection:
                return "the connection identifier is invalid";
            case PlayerLifeDecision::InvalidRespawnType:
                return "the respawn type is invalid";
            case PlayerLifeDecision::AlreadyDead:
                return "the player is already dead or respawning";
            case PlayerLifeDecision::NotDead:
                return "the player is not dead";
            case PlayerLifeDecision::NotRespawning:
                return "the player has no pending respawn";
            case PlayerLifeDecision::RespawnTypeMismatch:
                return "the respawn acknowledgement does not match the server result";
            case PlayerLifeDecision::CapacityReached:
                return "the lifecycle store is at capacity";
        }
        return "unknown player lifecycle result";
    }
}
