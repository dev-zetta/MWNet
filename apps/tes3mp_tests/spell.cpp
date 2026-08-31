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
        return { health, health, magicka, magicka, 1, 0, position, health > 0 };
    }

    SpellDefinition fireball()
    {
        SpellDefinition definition;
        definition.id = "fireball";
        definition.displayName = "Fireball";
        definition.range = SpellRange::Target;
        definition.magickaCost = 10;
        definition.baseSuccessChance = 0.75;
        definition.maximumRange = 1024;
        definition.effects = {
            { "fire damage", {}, SpellEffectKind::DamageHealth, 10, 20, 0 },
            { "weakness to fire", {}, SpellEffectKind::Timed, 20, 20, 5 },
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
        EXPECT(result.activeSpell->effects.size() == 1);
        EXPECT(result.activeSpell->effects[0].magnitude == 20);
        EXPECT(resolver.findCombatant(caster)->magicka == 40);
        EXPECT(!resolver.findCombatant(target)->alive);
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
    testDefinitionsAndCapacityFailClosed();
    return sFailures;
}
