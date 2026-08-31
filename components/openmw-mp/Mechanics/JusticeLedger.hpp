#ifndef OPENMW_MP_MECHANICS_JUSTICE_LEDGER_HPP
#define OPENMW_MP_MECHANICS_JUSTICE_LEDGER_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace mwmp::mechanics
{
    struct JailSentence
    {
        std::uint64_t id = 0;
        std::uint32_t days = 0;
        bool ignoreTeleportation = false;
        bool ignoreSkillIncreases = false;
        std::string progressText;
        std::string endText;

        bool operator==(const JailSentence&) const = default;
    };

    struct JusticeState
    {
        std::int64_t bounty = 0;
        std::optional<JailSentence> sentence;

        bool operator==(const JusticeState&) const = default;
    };

    enum class JusticeDecision : std::uint8_t
    {
        Applied,
        InvalidPlayer,
        InvalidBounty,
        UnauthorizedBountyReduction,
        InvalidSentence,
        SentenceAlreadyActive,
        SentenceNotActive,
        StaleSentence,
        CapacityReached,
    };

    struct JusticeResult
    {
        JusticeDecision decision = JusticeDecision::InvalidPlayer;
        JusticeState state;

        bool applied() const noexcept
        {
            return decision == JusticeDecision::Applied;
        }
    };

    class JusticeLedger
    {
    public:
        static constexpr std::int64_t MaximumBounty = 1'000'000'000;
        static constexpr std::uint32_t MaximumJailDays = 365'000;
        static constexpr std::size_t MaximumTextBytes = 4096;

        explicit JusticeLedger(std::size_t maximumPlayers = 4096);

        JusticeResult setBounty(std::uint64_t player, std::int64_t bounty);
        JusticeResult previewBountyIntent(
            std::uint64_t player, std::int64_t reportedBounty) const;
        JusticeResult applyBountyIntent(
            std::uint64_t player, std::int64_t reportedBounty);
        JusticeResult beginSentence(std::uint64_t player, std::uint32_t days,
            bool ignoreTeleportation, bool ignoreSkillIncreases,
            std::string progressText, std::string endText);
        JusticeResult previewSentenceCompletion(
            std::uint64_t player, std::uint64_t sentenceId) const;
        JusticeResult completeSentence(
            std::uint64_t player, std::uint64_t sentenceId, bool clearBounty);

        std::optional<JusticeState> find(std::uint64_t player) const;
        bool erase(std::uint64_t player) noexcept;
        void clear() noexcept;
        std::size_t size() const noexcept;

    private:
        static bool validBounty(std::int64_t bounty) noexcept;
        static bool validText(const std::string& text) noexcept;

        std::size_t mMaximumPlayers;
        std::uint64_t mNextSentenceId = 1;
        std::unordered_map<std::uint64_t, JusticeState> mPlayers;
    };

    const char* describe(JusticeDecision decision) noexcept;
}

#endif
