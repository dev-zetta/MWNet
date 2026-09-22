#ifndef OPENMW_MP_MECHANICS_ACTOR_STATE_LEDGER_HPP
#define OPENMW_MP_MECHANICS_ACTOR_STATE_LEDGER_HPP

#include "EquipmentLedger.hpp"
#include "MovementValidator.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace mwmp::mechanics
{
    struct ActorIdentity
    {
        std::string cell;
        std::uint32_t refNum = 0;
        std::uint32_t mpNum = 0;

        bool operator==(const ActorIdentity&) const = default;
    };

    struct ActorIdentityHash
    {
        std::size_t operator()(const ActorIdentity& identity) const noexcept;
    };

    struct ActorEquipmentUpdate
    {
        ActorIdentity identity;
        EquipmentLedger::Equipment equipment;
    };

    struct ActorTransform
    {
        Position3 position;
        Position3 rotation;
        Position3 direction;
        Position3 directionRotation;
    };

    struct ActorPositionUpdate
    {
        ActorIdentity identity;
        ActorTransform transform;
        std::uint64_t sequence = 0;
    };

    struct ActorCellChangeUpdate
    {
        ActorIdentity source;
        std::string destinationCell;
        ActorTransform transform;
        std::uint64_t sequence = 0;
    };

    enum class ActorAiAction : std::uint8_t
    {
        Cancel,
        Activate,
        Combat,
        Escort,
        Follow,
        Travel,
        Wander,
    };

    enum class ActorAiTargetKind : std::uint8_t
    {
        Player,
        Reference,
    };

    struct ActorAiTarget
    {
        ActorAiTargetKind kind = ActorAiTargetKind::Reference;
        std::uint64_t player = 0;
        ActorIdentity reference;

        bool operator==(const ActorAiTarget&) const = default;
    };

    struct ActorAiState
    {
        ActorAiAction action = ActorAiAction::Cancel;
        std::optional<ActorAiTarget> target;
        Position3 coordinates;
        std::uint32_t distance = 0;
        std::uint32_t duration = 0;
        bool repeat = false;

        bool operator==(const ActorAiState&) const = default;
    };

    struct ActorAiUpdate
    {
        ActorIdentity identity;
        ActorAiState state;

        bool operator==(const ActorAiUpdate&) const = default;
    };

    enum class ActorRosterAction : std::uint8_t
    {
        Set,
        Add,
        Remove,
    };

    struct ActorRosterUpdate
    {
        ActorIdentity identity;
        std::string refId;
    };

    enum class ActorStateDecision : std::uint8_t
    {
        Applied,
        InvalidBatch,
        InvalidIdentity,
        DuplicateActor,
        InvalidEquipment,
        InvalidPosition,
        InvalidSpeed,
        InvalidSequence,
        StaleSequence,
        SpeedExceeded,
        InvalidRosterAction,
        InvalidRefId,
        UnknownActor,
        DestinationOccupied,
        InvalidAiState,
        InvalidAiTarget,
        ActorLimitReached,
    };

    struct ActorStateResult
    {
        ActorStateDecision decision = ActorStateDecision::InvalidBatch;
        std::size_t actorCount = 0;
        double distance = 0;
        double allowedDistance = 0;

        bool applied() const noexcept
        {
            return decision == ActorStateDecision::Applied;
        }
    };

    class ActorStateLedger
    {
    public:
        static constexpr std::size_t MaximumChanges = 3000;
        static constexpr std::size_t MaximumCellBytes = 4096;
        static constexpr std::size_t MaximumRefIdBytes = 4096;
        static constexpr std::size_t DefaultMaximumActors = 100'000;
        static constexpr double DistanceMultiplier = 1.5;
        static constexpr std::chrono::milliseconds LatencyAllowance{ 250 };

        using Clock = std::chrono::steady_clock;

        explicit ActorStateLedger(std::size_t maximumActors = DefaultMaximumActors);

        ActorStateResult previewEquipment(
            const std::vector<ActorEquipmentUpdate>& updates) const;
        ActorStateResult applyEquipment(
            const std::vector<ActorEquipmentUpdate>& updates);
        ActorStateResult previewPositions(
            const std::vector<ActorPositionUpdate>& updates,
            double theoreticalMaximumSpeed, Clock::time_point now) const;
        ActorStateResult applyPositions(
            const std::vector<ActorPositionUpdate>& updates,
            double theoreticalMaximumSpeed, Clock::time_point now);
        // Server-only relocation: retain sequence protection and reset the movement baseline.
        bool recoverPosition(const ActorIdentity& identity, const ActorTransform& transform, Clock::time_point now);
        ActorStateResult previewCellChanges(
            const std::vector<ActorCellChangeUpdate>& updates) const;
        ActorStateResult applyCellChanges(
            const std::vector<ActorCellChangeUpdate>& updates,
            Clock::time_point now);
        ActorStateResult previewAi(const std::vector<ActorAiUpdate>& updates) const;
        ActorStateResult applyAi(const std::vector<ActorAiUpdate>& updates);
        ActorStateResult previewRoster(ActorRosterAction action,
            const std::string& cell,
            const std::vector<ActorRosterUpdate>& updates) const;
        ActorStateResult applyRoster(ActorRosterAction action,
            const std::string& cell,
            const std::vector<ActorRosterUpdate>& updates);
        std::optional<EquipmentLedger::Equipment> equipment(
            const ActorIdentity& identity) const;
        std::optional<ActorTransform> position(const ActorIdentity& identity) const;
        std::optional<std::string> refId(const ActorIdentity& identity) const;
        std::optional<ActorAiState> ai(const ActorIdentity& identity) const;
        bool contains(const ActorIdentity& identity) const noexcept;
        std::vector<ActorIdentity> identities(const std::string& cell) const;
        std::size_t eraseCell(const std::string& cell) noexcept;
        void swap(ActorStateLedger& other) noexcept;
        void clear() noexcept;
        std::size_t size() const noexcept;

    private:
        ActorStateResult validate(
            const std::vector<ActorEquipmentUpdate>& updates) const;
        ActorStateResult validatePositions(
            const std::vector<ActorPositionUpdate>& updates,
            double theoreticalMaximumSpeed, Clock::time_point now) const;
        ActorStateResult validateCellChanges(
            const std::vector<ActorCellChangeUpdate>& updates) const;
        ActorStateResult validateAi(const std::vector<ActorAiUpdate>& updates) const;
        ActorStateResult validateRoster(ActorRosterAction action,
            const std::string& cell,
            const std::vector<ActorRosterUpdate>& updates) const;
        static bool validIdentity(const ActorIdentity& identity) noexcept;
        static bool validEquipment(const EquipmentLedger::Equipment& equipment) noexcept;
        static bool validTransform(const ActorTransform& transform) noexcept;
        static double distance(const Position3& left, const Position3& right) noexcept;

        struct ActorMovementState
        {
            ActorTransform transform;
            std::uint64_t sequence = 0;
            Clock::time_point observedAt;
        };

        struct ActorState
        {
            std::string refId;
            std::optional<EquipmentLedger::Equipment> equipment;
            std::optional<ActorMovementState> movement;
            std::optional<ActorAiState> ai;
        };

        std::size_t mMaximumActors;
        std::unordered_map<ActorIdentity, ActorState, ActorIdentityHash> mActors;
    };

    const char* describe(ActorStateDecision decision) noexcept;
}

#endif
