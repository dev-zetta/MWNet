#ifndef OPENMW_MP_MECHANICS_PLAYER_PROGRESSION_LEDGER_HPP
#define OPENMW_MP_MECHANICS_PLAYER_PROGRESSION_LEDGER_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>

namespace mwmp::mechanics
{
    struct ProgressionStat
    {
        float base = 0;
        float modifier = 0;
        float current = 0;
        float damage = 0;
        float progress = 0;

        bool operator==(const ProgressionStat&) const = default;
    };

    struct PlayerProgressionState
    {
        static constexpr std::size_t AttributeCount = 8;
        static constexpr std::size_t SkillCount = 27;

        std::array<ProgressionStat, AttributeCount> attributes{};
        std::array<std::int32_t, AttributeCount> skillIncreases{};
        std::array<ProgressionStat, SkillCount> skills{};
        std::int32_t level = 1;
        std::int32_t levelProgress = 0;

        bool operator==(const PlayerProgressionState&) const = default;
    };

    struct AttributeProgressionChange
    {
        std::uint8_t index = 0;
        ProgressionStat stat;
        std::int32_t skillIncrease = 0;
    };

    struct SkillProgressionChange
    {
        std::uint8_t index = 0;
        ProgressionStat stat;
    };

    enum class ProgressionDecision : std::uint8_t
    {
        Applied,
        InvalidPlayer,
        UnknownPlayer,
        InvalidStat,
        InvalidIndex,
        DuplicateIndex,
        FullClientSnapshot,
        UnauthorizedAttributeChange,
        UnauthorizedSkillChange,
        UnauthorizedLevelChange,
        CapacityReached,
    };

    struct ProgressionResult
    {
        ProgressionDecision decision = ProgressionDecision::InvalidPlayer;
        PlayerProgressionState state;

        bool applied() const noexcept
        {
            return decision == ProgressionDecision::Applied;
        }
    };

    class PlayerProgressionLedger
    {
    public:
        static constexpr float MaximumStatValue = 1'000'000.0f;
        static constexpr std::int32_t MaximumLevel = 1000;
        static constexpr std::int32_t MaximumLevelProgress = 1000;
        static constexpr std::int32_t MaximumSkillIncrease = 1000;

        explicit PlayerProgressionLedger(std::size_t maximumPlayers = 4096);

        ProgressionResult set(
            std::uint64_t player, PlayerProgressionState state);
        ProgressionResult previewAttributes(std::uint64_t player, bool fullSnapshot,
            std::span<const AttributeProgressionChange> changes) const;
        ProgressionResult applyAttributes(std::uint64_t player, bool fullSnapshot,
            std::span<const AttributeProgressionChange> changes);
        ProgressionResult previewSkills(std::uint64_t player, bool fullSnapshot,
            std::span<const SkillProgressionChange> changes) const;
        ProgressionResult applySkills(std::uint64_t player, bool fullSnapshot,
            std::span<const SkillProgressionChange> changes);
        ProgressionResult previewLevel(std::uint64_t player,
            std::int32_t level, std::int32_t levelProgress) const;
        ProgressionResult applyLevel(std::uint64_t player,
            std::int32_t level, std::int32_t levelProgress);

        std::optional<PlayerProgressionState> find(std::uint64_t player) const;
        bool erase(std::uint64_t player) noexcept;
        void clear() noexcept;
        std::size_t size() const noexcept;

    private:
        static bool validStat(const ProgressionStat& stat) noexcept;
        static bool validState(const PlayerProgressionState& state) noexcept;

        std::size_t mMaximumPlayers;
        std::unordered_map<std::uint64_t, PlayerProgressionState> mPlayers;
    };

    const char* describe(ProgressionDecision decision) noexcept;
}

#endif
