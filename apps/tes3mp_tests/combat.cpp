#include <components/openmw-mp/Mechanics/CombatResolver.hpp>

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
        std::cerr << "combat.cpp:" << line << ": expectation failed: " << expression << '\n';
        ++sFailures;
    }

#define EXPECT(condition) expect((condition), #condition, __LINE__)

    CombatantState state(double health, Position3 position)
    {
        CombatantState result;
        result.health = health;
        result.maximumHealth = health;
        result.fatigueRatio = 1;
        result.accuracy = 0.8;
        result.evasion = 0.1;
        result.armorRating = 25;
        result.minimumDamage = 10;
        result.maximumDamage = 30;
        result.meleeReach = 128;
        result.projectileReach = 4096;
        result.position = position;
        result.alive = health > 0;
        return result;
    }

    void testCanonicalHitAndDeath()
    {
        CombatResolver resolver;
        const CombatantId attacker{ CombatantKind::Player, 7, {} };
        const CombatantId target{ CombatantKind::Actor, 11, "Balmora" };
        EXPECT(resolver.upsert(attacker, state(100, { 0, 0, 0 })));
        EXPECT(resolver.upsert(target, state(16, { 64, 0, 0 })));

        const CombatResult hit = resolver.resolve(
            { attacker, target, 1, AttackKind::Melee, 0.5 }, 0.25);
        EXPECT(hit.decision == CombatDecision::AppliedHit);
        EXPECT(std::abs(hit.hitChance - 0.7) < 0.0001);
        EXPECT(std::abs(hit.damage - 16.0) < 0.0001);
        EXPECT(hit.targetHealth == 0);
        EXPECT(hit.targetDied);
        EXPECT(!resolver.find(target)->alive);
        EXPECT(resolver.resolve({ attacker, target, 2, AttackKind::Melee, 1 }, 0)
            .decision == CombatDecision::TargetDead);
    }

    void testMissRangeAndSequence()
    {
        CombatResolver resolver;
        const CombatantId attacker{ CombatantKind::Player, 1, {} };
        const CombatantId target{ CombatantKind::Player, 2, {} };
        EXPECT(resolver.upsert(attacker, state(100, { 0, 0, 0 })));
        EXPECT(resolver.upsert(target, state(100, { 1000, 0, 0 })));

        EXPECT(resolver.resolve({ attacker, target, 1, AttackKind::Melee, 1 }, 0)
            .decision == CombatDecision::OutOfRange);
        EXPECT(resolver.resolve({ attacker, target, 1, AttackKind::Ranged, 1 }, 0)
            .decision == CombatDecision::StaleSequence);
        const CombatResult miss = resolver.resolve(
            { attacker, target, 2, AttackKind::Ranged, 1 }, 0.9);
        EXPECT(miss.decision == CombatDecision::AppliedMiss);
        EXPECT(miss.damage == 0);
        EXPECT(miss.targetHealth == 100);
    }

    void testInvalidDataAndCapacity()
    {
        CombatResolver resolver(1);
        const CombatantId first{ CombatantKind::Player, 1, {} };
        const CombatantId second{ CombatantKind::Actor, 2, "Seyda Neen" };
        EXPECT(!resolver.upsert({}, state(10, {})));
        EXPECT(resolver.upsert(first, state(10, {})));
        EXPECT(!resolver.upsert(second, state(10, {})));

        CombatantState invalid = state(10, {});
        invalid.health = std::numeric_limits<double>::quiet_NaN();
        EXPECT(!resolver.upsert(first, invalid));
        EXPECT(resolver.resolve({ first, second, 0, AttackKind::Melee, 1 }, 0)
            .decision == CombatDecision::InvalidSequence);
        EXPECT(resolver.resolve({ first, second, 1, AttackKind::Melee, 2 }, 0)
            .decision == CombatDecision::InvalidIntent);
        EXPECT(resolver.resolve({ first, second, 1, AttackKind::Melee, 1 }, 1)
            .decision == CombatDecision::InvalidIntent);
        EXPECT(std::string(describe(CombatDecision::OutOfRange))
            == "the target is outside the server-approved range");
        EXPECT(resolver.erase(first));
        EXPECT(resolver.size() == 0);
    }

    void testActorIdentityIsCellScoped()
    {
        CombatResolver resolver;
        const CombatantId balmoraActor{ CombatantKind::Actor, 42, "Balmora" };
        const CombatantId vivecActor{ CombatantKind::Actor, 42, "Vivec" };
        const CombatantId unscopedActor{ CombatantKind::Actor, 42, {} };

        EXPECT(resolver.upsert(balmoraActor, state(20, {})));
        EXPECT(resolver.upsert(vivecActor, state(30, {})));
        EXPECT(!resolver.upsert(unscopedActor, state(40, {})));
        EXPECT(resolver.size() == 2);
        EXPECT(resolver.find(balmoraActor)->health == 20);
        EXPECT(resolver.find(vivecActor)->health == 30);
    }

    void testActorRelocationIsAtomic()
    {
        CombatResolver resolver;
        const CombatantId source{ CombatantKind::Actor, 42, "Balmora" };
        const CombatantId destination{ CombatantKind::Actor, 42, "Vivec" };
        EXPECT(resolver.upsert(source, state(30, { 1, 2, 3 })));
        EXPECT(resolver.previewRelocations(
            { { source, destination, { 40, 50, 60 } } }));
        EXPECT(resolver.applyRelocations(
            { { source, destination, { 40, 50, 60 } } }));
        EXPECT(!resolver.find(source));
        EXPECT(resolver.find(destination)->health == 30);
        EXPECT(resolver.find(destination)->position.x == 40);

        const CombatantId occupied{ CombatantKind::Actor, 42, "Seyda Neen" };
        EXPECT(resolver.upsert(occupied, state(10, {})));
        EXPECT(!resolver.applyRelocations(
            { { destination, occupied, { 1, 1, 1 } } }));
        EXPECT(resolver.find(destination)->health == 30);
        EXPECT(resolver.find(occupied)->health == 10);
        EXPECT(!resolver.applyRelocations(
            { { destination, destination, {} } }));
    }
}

int runCombatTests()
{
    testCanonicalHitAndDeath();
    testMissRangeAndSequence();
    testInvalidDataAndCapacity();
    testActorIdentityIsCellScoped();
    testActorRelocationIsAtomic();
    return sFailures;
}
