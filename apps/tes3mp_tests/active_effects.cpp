#include <components/openmw-mp/Mechanics/ActiveEffectLedger.hpp>

#include <iostream>
#include <limits>
#include <string>
#include <utility>

namespace
{
    using namespace mwmp::mechanics;

    int sFailures = 0;

    void expect(bool condition, const char* expression, int line)
    {
        if (condition)
            return;
        std::cerr << "active_effects.cpp:" << line << ": expectation failed: "
                  << expression << '\n';
        ++sFailures;
    }

#define EXPECT(condition) expect((condition), #condition, __LINE__)

    CanonicalActiveSpell spell(std::string id, bool stacking = false)
    {
        CanonicalActiveSpell result;
        result.id = std::move(id);
        result.displayName = result.id;
        result.stacking = stacking;
        result.effects.push_back({ "fire_damage", {}, 10, 5, 5 });
        return result;
    }

    CanonicalActiveSpell selector(std::string id)
    {
        CanonicalActiveSpell result;
        result.id = std::move(id);
        return result;
    }

    void testSetAddRemove()
    {
        ActiveEffectLedger ledger;
        const CombatantId player{ CombatantKind::Player, 7, {} };
        EXPECT(ledger.apply(player, ActiveEffectAction::Set,
                   { spell("fire"), spell("poison") }).applied());
        EXPECT(ledger.apply(player, ActiveEffectAction::Add,
                   { spell("fire") }).applied());
        EXPECT(ledger.snapshot(player)->size() == 2);

        EXPECT(ledger.apply(player, ActiveEffectAction::Add,
                   { spell("stack", true), spell("stack", true) }).applied());
        EXPECT(ledger.snapshot(player)->size() == 4);
        EXPECT(ledger.apply(player, ActiveEffectAction::Remove,
                   { selector("stack") }).applied());
        EXPECT(ledger.snapshot(player)->size() == 3);
    }

    void testFailureIsTransactional()
    {
        ActiveEffectLedger ledger;
        const CombatantId player{ CombatantKind::Player, 1, {} };
        EXPECT(ledger.apply(player, ActiveEffectAction::Set,
                   { spell("fire") }).applied());

        CanonicalActiveSpell invalid = spell("invalid");
        invalid.effects.front().timeLeft = 6;
        EXPECT(ledger.apply(player, ActiveEffectAction::Add,
                   { spell("poison"), invalid }).decision
            == ActiveEffectDecision::InvalidSpell);
        EXPECT(ledger.snapshot(player)->size() == 1);
        EXPECT(ledger.apply(player, ActiveEffectAction::Remove,
                   { spell("missing") }).decision == ActiveEffectDecision::MissingSpell);
        EXPECT(ledger.snapshot(player)->front().id == "fire");
    }

    void testLimitsAndScopedOwners()
    {
        ActiveEffectLedger ledger(2);
        const CombatantId balmora{ CombatantKind::Actor, 42, "Balmora" };
        const CombatantId vivec{ CombatantKind::Actor, 42, "Vivec" };
        EXPECT(ledger.apply(balmora, ActiveEffectAction::Set,
                   { spell("fire") }).applied());
        EXPECT(ledger.apply(vivec, ActiveEffectAction::Set,
                   { spell("frost") }).applied());
        EXPECT(ledger.snapshot(balmora)->front().id == "fire");
        EXPECT(ledger.snapshot(vivec)->front().id == "frost");
        EXPECT(ledger.apply({ CombatantKind::Actor, 42, {} },
                   ActiveEffectAction::Set, {}).decision == ActiveEffectDecision::InvalidOwner);

        CanonicalActiveSpell invalid = spell("nan");
        invalid.effects.front().magnitude = std::numeric_limits<double>::quiet_NaN();
        EXPECT(ledger.preview(balmora, ActiveEffectAction::Set, { invalid }).decision
            == ActiveEffectDecision::InvalidSpell);
        EXPECT(std::string(describe(ActiveEffectDecision::MissingSpell))
            == "the active spell does not exist");
    }
}

int runActiveEffectTests()
{
    testSetAddRemove();
    testFailureIsTransactional();
    testLimitsAndScopedOwners();
    return sFailures;
}
