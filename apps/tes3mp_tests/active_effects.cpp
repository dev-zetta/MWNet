#include <components/openmw-mp/Mechanics/ActiveEffectLedger.hpp>

#include <algorithm>
#include <cmath>
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

    void testBatchIsAtomic()
    {
        ActiveEffectLedger ledger;
        const CombatantId first{ CombatantKind::Actor, 8, "Balmora" };
        const CombatantId second{ CombatantKind::Actor, 9, "Balmora" };
        EXPECT(ledger.apply(first, ActiveEffectAction::Set,
                   { spell("fire") }).applied());
        EXPECT(ledger.apply(second, ActiveEffectAction::Set,
                   { spell("frost") }).applied());

        const std::vector<ActiveEffectOperation> invalid{
            { first, ActiveEffectAction::Remove, { selector("fire") } },
            { second, ActiveEffectAction::Remove, { selector("missing") } },
        };
        EXPECT(ledger.previewBatch(invalid).decision
            == ActiveEffectDecision::MissingSpell);
        EXPECT(ledger.applyBatch(invalid).decision
            == ActiveEffectDecision::MissingSpell);
        EXPECT(ledger.snapshot(first)->front().id == "fire");
        EXPECT(ledger.snapshot(second)->front().id == "frost");

        const std::vector<ActiveEffectOperation> valid{
            { first, ActiveEffectAction::Remove, { selector("fire") } },
            { second, ActiveEffectAction::Add, { spell("poison") } },
        };
        EXPECT(ledger.previewBatch(valid).applied());
        EXPECT(ledger.snapshot(first)->size() == 1);
        EXPECT(ledger.applyBatch(valid).applied());
        EXPECT(ledger.snapshot(first)->empty());
        EXPECT(ledger.snapshot(second)->size() == 2);
    }

    void testActorRelocationPreservesEffectsAndCasters()
    {
        ActiveEffectLedger ledger;
        const CombatantId source{ CombatantKind::Actor, 42, "Balmora" };
        const CombatantId destination{ CombatantKind::Actor, 42, "Vivec" };
        const CombatantId other{ CombatantKind::Actor, 99, "Vivec" };
        CanonicalActiveSpell sourceSpell = spell("fire");
        sourceSpell.caster = source;
        CanonicalActiveSpell otherSpell = spell("frost");
        otherSpell.caster = source;
        EXPECT(ledger.apply(source, ActiveEffectAction::Set,
                   { sourceSpell }).applied());
        EXPECT(ledger.apply(other, ActiveEffectAction::Set,
                   { otherSpell }).applied());

        const std::vector<CombatantRelocation> relocation{
            { source, destination, {} },
        };
        EXPECT(ledger.previewRelocations(relocation));
        EXPECT(ledger.applyRelocations(relocation));
        EXPECT(!ledger.snapshot(source));
        EXPECT(ledger.snapshot(destination)->front().id == "fire");
        EXPECT(ledger.snapshot(destination)->front().caster == destination);
        EXPECT(ledger.snapshot(other)->front().caster == destination);

        EXPECT(!ledger.applyRelocations(
            { { destination, other, {} } }));
        EXPECT(ledger.snapshot(destination)->front().id == "fire");
        EXPECT(ledger.snapshot(other)->front().id == "frost");
    }

    const ActiveEffectTick* findTick(const ActiveEffectAdvanceResult& result,
        const CombatantId& owner)
    {
        const auto found = std::ranges::find(result.changes, owner,
            &ActiveEffectTick::owner);
        return found == result.changes.end() ? nullptr : &*found;
    }

    void testServerClockAppliesAndExpiresHealthEffects()
    {
        ActiveEffectLedger ledger;
        const CombatantId caster{ CombatantKind::Player, 1, {} };
        const CombatantId target{ CombatantKind::Player, 2, {} };
        CanonicalActiveSpell damage = spell("elemental");
        damage.caster = caster;
        damage.effects = {
            { "Fire Damage", {}, 8, 2, 2 },
            { "restore_health", {}, 2, 1, 1 },
        };
        EXPECT(ledger.apply(target, ActiveEffectAction::Set, { damage }).applied());

        const ActiveEffectAdvanceResult first = ledger.advance(0.5);
        const ActiveEffectTick* firstTarget = findTick(first, target);
        EXPECT(first.applied());
        EXPECT(firstTarget != nullptr);
        EXPECT(std::abs(firstTarget->healthDelta + 3.0) < 0.000001);
        EXPECT(!firstTarget->topologyChanged);
        EXPECT(std::abs(ledger.snapshot(target)->front().effects.front().timeLeft
            - 1.5) < 0.000001);

        const ActiveEffectAdvanceResult second = ledger.advance(0.5);
        const ActiveEffectTick* secondTarget = findTick(second, target);
        EXPECT(secondTarget != nullptr);
        EXPECT(std::abs(secondTarget->healthDelta + 3.0) < 0.000001);
        EXPECT(secondTarget->topologyChanged);
        EXPECT(ledger.snapshot(target)->front().effects.size() == 1);

        const ActiveEffectAdvanceResult final = ledger.advance(1.0);
        EXPECT(findTick(final, target)->topologyChanged);
        EXPECT(!ledger.snapshot(target));
    }

    void testAbsorbHealthCreditsCanonicalCaster()
    {
        ActiveEffectLedger ledger;
        const CombatantId caster{ CombatantKind::Actor, 3, "Balmora" };
        const CombatantId target{ CombatantKind::Player, 4, {} };
        CanonicalActiveSpell absorb = spell("absorb");
        absorb.caster = caster;
        absorb.effects = { { "AbsorbHealth", {}, 5, 2, 2 } };
        EXPECT(ledger.apply(target, ActiveEffectAction::Set, { absorb }).applied());

        const ActiveEffectAdvanceResult result = ledger.advance(0.25);
        EXPECT(std::abs(findTick(result, target)->healthDelta + 1.25) < 0.000001);
        EXPECT(std::abs(findTick(result, caster)->healthDelta - 1.25) < 0.000001);
    }

    void testInvalidClockAdvanceIsTransactional()
    {
        ActiveEffectLedger ledger;
        const CombatantId player{ CombatantKind::Player, 7, {} };
        EXPECT(ledger.apply(player, ActiveEffectAction::Set,
                   { spell("fire") }).applied());
        EXPECT(ledger.advance(-1).decision
            == ActiveEffectAdvanceDecision::InvalidElapsed);
        EXPECT(ledger.advance(std::numeric_limits<double>::quiet_NaN()).decision
            == ActiveEffectAdvanceDecision::InvalidElapsed);
        EXPECT(ledger.snapshot(player)->front().effects.front().timeLeft == 5);
        EXPECT(std::string(describe(ActiveEffectAdvanceDecision::InvalidElapsed))
            == "the active-effect elapsed time is invalid");
    }
}

int runActiveEffectTests()
{
    testSetAddRemove();
    testFailureIsTransactional();
    testLimitsAndScopedOwners();
    testBatchIsAtomic();
    testActorRelocationPreservesEffectsAndCasters();
    testServerClockAppliesAndExpiresHealthEffects();
    testAbsorbHealthCreditsCanonicalCaster();
    testInvalidClockAdvanceIsTransactional();
    return sFailures;
}
