#ifndef OPENMW_MP_MECHANICS_PLAYER_LIFECYCLE_HPP
#define OPENMW_MP_MECHANICS_PLAYER_LIFECYCLE_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>

namespace mwmp::mechanics
{
    enum class PlayerLifeState : std::uint8_t
    {
        Alive,
        Dead,
        Respawning,
    };

    enum class PlayerLifeDecision : std::uint8_t
    {
        Applied,
        InvalidConnection,
        InvalidRespawnType,
        AlreadyDead,
        NotDead,
        NotRespawning,
        RespawnTypeMismatch,
        CapacityReached,
    };

    struct PlayerLifeTransition
    {
        PlayerLifeDecision decision = PlayerLifeDecision::InvalidConnection;
        PlayerLifeState state = PlayerLifeState::Alive;
        std::uint64_t generation = 0;

        bool applied() const noexcept { return decision == PlayerLifeDecision::Applied; }
    };

    class PlayerLifecycle
    {
    public:
        static constexpr std::uint32_t MaximumRespawnType = 2;

        explicit PlayerLifecycle(std::size_t maximumPlayers = 4096);

        PlayerLifeTransition reportDeath(std::uint64_t connection);
        PlayerLifeTransition beginRespawn(
            std::uint64_t connection, std::uint32_t respawnType);
        PlayerLifeTransition acknowledgeRespawn(
            std::uint64_t connection, std::uint32_t respawnType);

        PlayerLifeState state(std::uint64_t connection) const noexcept;
        std::optional<std::uint32_t> pendingRespawnType(
            std::uint64_t connection) const noexcept;
        bool erase(std::uint64_t connection) noexcept;
        void clear() noexcept;
        std::size_t size() const noexcept;

    private:
        struct State
        {
            PlayerLifeState value = PlayerLifeState::Alive;
            std::uint32_t respawnType = 0;
            std::uint64_t generation = 0;
        };

        State* getOrCreate(std::uint64_t connection);

        std::size_t mMaximumPlayers;
        std::unordered_map<std::uint64_t, State> mStates;
    };

    const char* describe(PlayerLifeDecision decision) noexcept;
}

#endif
