#include <components/openmw-mp/Mechanics/SpellResolver.hpp>

#include <cmath>
#include <iostream>
#include <limits>
#include <string>

namespace
{
    using namespace mwmp::mechanics;

    int sFailures = 0;

    void expect(bool condition, const char* expression, int line)
    {
        if (condition)
            return;
        std::cerr << "spell.cpp:" << line << ": expectation failed: "
                  << expression << '\n';
        ++sFailures;
    }

#define EXPECT(condition) expect((condition), #condition, __LINE__)

    SpellCombatantState combatant(
        double health, double magicka, Position3 position)
    {
        SpellCombatantState state;
        state.health = health;
        state.maximumHealth = health;
        state.magicka = magicka;
        state.maximumMagicka = magicka;
        state.position = position;
        state.alive = health > 0;
        return state;
    }

    SpellDefinition fireball()
    {
        SpellDefinition definition;
        definition.id = "fireball";
        definition.displayName = "Fireball";
        definition.magickaCost = 10;
        definition.baseSuccessChance = 0.75;
        definition.effects = {
            { "fire damage", {}, SpellEffectKind::DamageHealth,
                SpellRange::Target, 10, 20, 0, 1024 },
            { "weakness to fire", {}, SpellEffectKind::Timed,
                SpellRange::Target, 20, 20, 5, 1024 },
        };
        return definition;
    }

    void testServerOwnsSuccessAndEffects()
    {
        SpellResolver resolver;
        const CombatantId caster{ CombatantKind::Player, 7, {} };
        const CombatantId target{ CombatantKind::Actor, 19, "Balmora" };
        EXPECT(resolver.upsertCombatant(caster, combatant(100, 50, {})));
        EXPECT(resolver.upsertCombatant(target, combatant(12, 0, { 100, 0, 0 })));
        EXPECT(resolver.upsertDefinition(fireball()));

        const SpellResult result = resolver.resolve(
            { caster, target, "fireball", 1 }, 0.25, 0.5);
        EXPECT(result.decision == SpellDecision::Applied);
        EXPECT(result.successChance == 0.75);
        EXPECT(result.magickaSpent == 10);
        EXPECT(result.targetHealth == 0);
        EXPECT(result.targetDied);
        EXPECT(result.activeSpell.has_value());
        EXPECT(result.applications.size() == 1);
        EXPECT(result.applications[0].target == target);
        EXPECT(result.activeSpell->effects.size() == 1);
        EXPECT(result.activeSpell->effects[0].magnitude == 20);
        EXPECT(resolver.findCombatant(caster)->magicka == 40);
        EXPECT(!resolver.findCombatant(target)->alive);
    }

    void testMixedRangesApplyToCanonicalTargets()
    {
        SpellResolver resolver;
        const CombatantId caster{ CombatantKind::Player, 1, {} };
        const CombatantId target{ CombatantKind::Player, 2, {} };
        EXPECT(resolver.upsertCombatant(caster, combatant(50, 50, {})));
        EXPECT(resolver.upsertCombatant(target, combatant(50, 50, { 10, 0, 0 })));

        SpellDefinition mixed;
        mixed.id = "mixed";
        mixed.displayName = "Mixed";
        mixed.magickaCost = 5;
        mixed.alwaysSucceeds = true;
        mixed.effects = {
            { "restore health", {}, SpellEffectKind::RestoreHealth,
                SpellRange::Self, 5, 5, 0, 0 },
            { "damage health", {}, SpellEffectKind::DamageHealth,
                SpellRange::Touch, 7, 7, 0, 64 },
        };
        SpellCombatantState wounded = *resolver.findCombatant(caster);
        wounded.health = 40;
        EXPECT(resolver.upsertCombatant(caster, wounded));
        EXPECT(resolver.upsertDefinition(mixed));

        const SpellResult result = resolver.resolve(
            { caster, target, "mixed", 1 }, 0, 0);
        EXPECT(result.decision == SpellDecision::Applied);
        EXPECT(result.applications.size() == 2);
        EXPECT(resolver.findCombatant(caster)->health == 45);
        EXPECT(resolver.findCombatant(target)->health == 43);
    }

    void testItemChargeIsCanonicalResource()
    {
        SpellResolver resolver;
        const CombatantId caster{ CombatantKind::Player, 1, {} };
        EXPECT(resolver.upsertCombatant(caster, combatant(50, 0, {})));

        SpellDefinition item;
        item.id = "ring_of_healing";
        item.displayName = "Ring of Healing";
        item.sourceKind = SpellSourceKind::Item;
        item.itemChargeCost = 12;
        item.itemMaximumCharge = 100;
        item.alwaysSucceeds = true;
        item.effects = {
            { "restore health", {}, SpellEffectKind::RestoreHealth,
                SpellRange::Self, 10, 10, 0, 0 },
        };
        EXPECT(resolver.upsertDefinition(item));
        EXPECT(resolver.resolve(
            { caster, std::nullopt, item.id, 1, 11 }, 0, 0).decision
            == SpellDecision::InsufficientItemCharge);
        const SpellResult result = resolver.resolve(
            { caster, std::nullopt, item.id, 1, 12 }, 0, 0);
        EXPECT(result.decision == SpellDecision::Applied);
        EXPECT(result.itemChargeSpent == 12);
        EXPECT(result.magickaSpent == 0);

        SpellResolver replacement;
        replacement.swap(resolver);
        EXPECT(!resolver.findCombatant(caster).has_value());
        EXPECT(replacement.findCombatant(caster).has_value());
    }

    void testMorrowindCastingFormulaUsesEffectiveSchool()
    {
        SpellResolver resolver;
        const CombatantId caster{ CombatantKind::Player, 1, {} };
        SpellCombatantState state = combatant(50, 50, {});
        state.willpower = 40;
        state.luck = 30;
        state.fatigueTerm = 0.75;
        state.magicSkills.emplace("destruction", 50);
        state.magicSkills.emplace("alteration", 35);
        EXPECT(resolver.upsertCombatant(caster, state));

        SpellDefinition spell;
        spell.id = "formula";
        spell.displayName = "Formula";
        spell.magickaCost = 20;
        spell.effects = {
            { "damage health", {}, SpellEffectKind::DamageHealth,
                SpellRange::Self, 1, 1, 0, 0, "destruction", 40 },
            { "shield", {}, SpellEffectKind::Timed,
                SpellRange::Self, 1, 1, 1, 0, "alteration", 20 },
        };
        EXPECT(resolver.upsertDefinition(spell));

        const SpellResult result = resolver.resolve(
            { caster, std::nullopt, spell.id, 1 }, 0.4, 0);
        // Alteration is effective: (70 - 20 + 8 + 3) * .75 = 45.75%.
        EXPECT(std::abs(result.successChance - 0.4575) < 0.000001);
        EXPECT(result.effectiveSchool == "alteration");
        EXPECT(result.decision == SpellDecision::Applied);

        state = *resolver.findCombatant(caster);
        state.silenced = true;
        EXPECT(resolver.upsertCombatant(caster, state));
        EXPECT(resolver.resolve(
            { caster, std::nullopt, spell.id, 2 }, 0, 0).decision
            == SpellDecision::Failed);
    }

    void testFailureConsumesOnlyCanonicalCost()
    {
        SpellResolver resolver;
        const CombatantId caster{ CombatantKind::Player, 1, {} };
        const CombatantId target{ CombatantKind::Player, 2, {} };
        EXPECT(resolver.upsertCombatant(caster, combatant(100, 25, {})));
        EXPECT(resolver.upsertCombatant(target, combatant(100, 10, { 20, 0, 0 })));
        EXPECT(resolver.upsertDefinition(fireball()));

        const SpellResult failed = resolver.resolve(
            { caster, target, "fireball", 4 }, 0.9, 0);
        EXPECT(failed.decision == SpellDecision::Failed);
        EXPECT(failed.magickaSpent == 10);
        EXPECT(resolver.findCombatant(caster)->magicka == 15);
        EXPECT(resolver.findCombatant(target)->health == 100);
        EXPECT(resolver.resolve({ caster, target, "fireball", 4 }, 0, 0)
            .decision == SpellDecision::StaleSequence);
    }

    void testRangeTargetAndResourceValidation()
    {
        SpellResolver resolver;
        const CombatantId caster{ CombatantKind::Player, 1, {} };
        const CombatantId target{ CombatantKind::Actor, 3, "Vivec" };
        EXPECT(resolver.upsertCombatant(caster, combatant(100, 9, {})));
        EXPECT(resolver.upsertCombatant(target, combatant(100, 0, { 2000, 0, 0 })));
        EXPECT(resolver.upsertDefinition(fireball()));
        EXPECT(resolver.resolve({ caster, target, "fireball", 1 }, 0, 0)
            .decision == SpellDecision::OutOfRange);

        SpellCombatantState nearTarget = *resolver.findCombatant(target);
        nearTarget.position = { 10, 0, 0 };
        EXPECT(resolver.upsertCombatant(target, nearTarget));
        EXPECT(resolver.resolve({ caster, target, "fireball", 1 }, 0, 0)
            .decision == SpellDecision::InsufficientMagicka);
        EXPECT(resolver.resolve({ caster, std::nullopt, "fireball", 1 }, 0, 0)
            .decision == SpellDecision::MissingTarget);
        EXPECT(resolver.resolve({ caster, target, "unknown", 1 }, 0, 0)
            .decision == SpellDecision::UnknownSpell);
    }

    void testDefinitionsAndCapacityFailClosed()
    {
        SpellResolver resolver(1, 1);
        const CombatantId first{ CombatantKind::Player, 1, {} };
        const CombatantId second{ CombatantKind::Player, 2, {} };
        EXPECT(resolver.upsertCombatant(first, combatant(10, 10, {})));
        EXPECT(!resolver.upsertCombatant(second, combatant(10, 10, {})));
        EXPECT(resolver.upsertDefinition(fireball()));

        SpellDefinition invalid = fireball();
        invalid.id = "other";
        EXPECT(!resolver.upsertDefinition(invalid));
        invalid = fireball();
        invalid.baseSuccessChance = std::numeric_limits<double>::quiet_NaN();
        EXPECT(!resolver.upsertDefinition(invalid));
        invalid = fireball();
        invalid.effects.resize(SpellResolver::MaximumEffectsPerSpell + 1);
        EXPECT(!resolver.upsertDefinition(invalid));
        EXPECT(std::string(describe(SpellDecision::OutOfRange))
            == "the target is outside the server-approved spell range");
    }
}

int runSpellTests()
{
    testServerOwnsSuccessAndEffects();
    testFailureConsumesOnlyCanonicalCost();
    testRangeTargetAndResourceValidation();
    testMixedRangesApplyToCanonicalTargets();
    testItemChargeIsCanonicalResource();
    testMorrowindCastingFormulaUsesEffectiveSchool();
    testDefinitionsAndCapacityFailClosed();
    return sFailures;
}
