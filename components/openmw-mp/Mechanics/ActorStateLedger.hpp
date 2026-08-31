#ifndef OPENMW_MP_MECHANICS_ACTOR_STATE_LEDGER_HPP
#define OPENMW_MP_MECHANICS_ACTOR_STATE_LEDGER_HPP

#include "EquipmentLedger.hpp"

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

    enum class ActorStateDecision : std::uint8_t
    {
        Applied,
        InvalidBatch,
        InvalidIdentity,
        DuplicateActor,
        InvalidEquipment,
        ActorLimitReached,
    };

    struct ActorStateResult
    {
        ActorStateDecision decision = ActorStateDecision::InvalidBatch;
        std::size_t actorCount = 0;

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
        static constexpr std::size_t DefaultMaximumActors = 100'000;

        explicit ActorStateLedger(std::size_t maximumActors = DefaultMaximumActors);

        ActorStateResult previewEquipment(
            const std::vector<ActorEquipmentUpdate>& updates) const;
        ActorStateResult applyEquipment(
            const std::vector<ActorEquipmentUpdate>& updates);
        std::optional<EquipmentLedger::Equipment> equipment(
            const ActorIdentity& identity) const;
        std::size_t eraseCell(const std::string& cell) noexcept;
        void clear() noexcept;
        std::size_t size() const noexcept;

    private:
        ActorStateResult validate(
            const std::vector<ActorEquipmentUpdate>& updates) const;
        static bool validIdentity(const ActorIdentity& identity) noexcept;
        static bool validEquipment(const EquipmentLedger::Equipment& equipment) noexcept;

        std::size_t mMaximumActors;
        std::unordered_map<ActorIdentity, EquipmentLedger::Equipment, ActorIdentityHash>
            mEquipment;
    };

    const char* describe(ActorStateDecision decision) noexcept;
}

#endif
