#ifndef OPENMW_MP_MECHANICS_SHAPESHIFT_LEDGER_HPP
#define OPENMW_MP_MECHANICS_SHAPESHIFT_LEDGER_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace mwmp::mechanics
{
    struct ShapeshiftState
    {
        double scale = 1.0;
        bool isWerewolf = false;
        bool displayCreatureName = false;
        std::string creatureRefId;

        bool operator==(const ShapeshiftState&) const = default;
    };

    enum class ShapeshiftDecision : std::uint8_t
    {
        Applied,
        InvalidPlayer,
        InvalidScale,
        InvalidCreatureRefId,
        UnauthorizedAppearanceChange,
        UnknownPlayer,
        CapacityReached,
    };

    struct ShapeshiftResult
    {
        ShapeshiftDecision decision = ShapeshiftDecision::InvalidPlayer;
        ShapeshiftState state;

        bool applied() const noexcept
        {
            return decision == ShapeshiftDecision::Applied;
        }
    };

    class ShapeshiftLedger
    {
    public:
        static constexpr double MinimumScale = 0.01;
        static constexpr double MaximumScale = 1000.0;
        static constexpr std::size_t MaximumCreatureRefIdBytes = 4096;

        explicit ShapeshiftLedger(std::size_t maximumPlayers = 4096);

        ShapeshiftResult set(std::uint64_t player, ShapeshiftState state);
        ShapeshiftResult previewClientIntent(
            std::uint64_t player, const ShapeshiftState& state) const;
        ShapeshiftResult applyClientIntent(
            std::uint64_t player, const ShapeshiftState& state);

        std::optional<ShapeshiftState> find(std::uint64_t player) const;
        bool erase(std::uint64_t player) noexcept;
        void clear() noexcept;
        std::size_t size() const noexcept;

    private:
        static ShapeshiftDecision validate(const ShapeshiftState& state) noexcept;

        std::size_t mMaximumPlayers;
        std::unordered_map<std::uint64_t, ShapeshiftState> mPlayers;
    };

    const char* describe(ShapeshiftDecision decision) noexcept;
}

#endif
