#ifndef OPENMW_MP_MECHANICS_SPELLBOOK_LEDGER_HPP
#define OPENMW_MP_MECHANICS_SPELLBOOK_LEDGER_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace mwmp::mechanics
{
    enum class SpellbookAction : std::uint8_t
    {
        Set,
        Add,
        Remove,
    };

    enum class SpellbookDecision : std::uint8_t
    {
        Applied,
        InvalidOwner,
        InvalidAction,
        InvalidSpell,
        MissingSpell,
        SpellLimitReached,
        OwnerLimitReached,
    };

    struct SpellbookResult
    {
        SpellbookDecision decision = SpellbookDecision::InvalidAction;
        std::size_t spellCount = 0;

        bool applied() const noexcept
        {
            return decision == SpellbookDecision::Applied;
        }
    };

    class SpellbookLedger
    {
    public:
        static constexpr std::size_t MaximumSpells = 4096;
        static constexpr std::size_t MaximumStringBytes = 4096;

        explicit SpellbookLedger(std::size_t maximumOwners = 4096);

        SpellbookResult preview(std::uint64_t owner, SpellbookAction action,
            const std::vector<std::string>& spellIds) const;
        SpellbookResult apply(std::uint64_t owner, SpellbookAction action,
            const std::vector<std::string>& spellIds);
        bool contains(std::uint64_t owner, const std::string& spellId) const;
        std::optional<std::vector<std::string>> snapshot(std::uint64_t owner) const;
        bool erase(std::uint64_t owner) noexcept;
        void clear() noexcept;
        std::size_t size() const noexcept;

    private:
        using SpellSet = std::unordered_set<std::string>;

        static bool validSpellId(const std::string& spellId) noexcept;
        static SpellbookResult applyTo(SpellSet& spellbook,
            SpellbookAction action, const std::vector<std::string>& spellIds);

        std::size_t mMaximumOwners;
        std::unordered_map<std::uint64_t, SpellSet> mSpellbooks;
    };

    const char* describe(SpellbookDecision decision) noexcept;
}

#endif
