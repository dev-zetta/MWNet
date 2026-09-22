#include <components/openmw-mp/Mechanics/CombatResolver.hpp>
#include <components/openmw-mp/Mechanics/AttackAnimation.hpp>

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
        result.fatigue = 100;
        result.maximumFatigue = 100;
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

    void testAttackAnimationPhases()
    {
        // Wind-up and targetless release/cancellation bypass damage resolution.
        EXPECT(isAttackAnimationOnly(true, false, false, 0, 0, 0));
        EXPECT(isAttackAnimationOnly(false, false, false, 0, 0, 0));
        // A hit claim or a specified target still requires combat validation.
        EXPECT(!isAttackAnimationOnly(false, true, false, 0, 0, 0));
        EXPECT(!isAttackAnimationOnly(false, false, true, 7, 0, 0));
        EXPECT(!isAttackAnimationOnly(false, false, false, 0, 128964, 0));
        EXPECT(!isAttackAnimationOnly(false, false, false, 0, 0, 9));
        // Stale/malformed player markers must not become targetless animations.
        EXPECT(!isAttackAnimationOnly(false, false, true, 0, 0, 0));
        EXPECT(!isAttackAnimationOnly(false, false, false, 7, 0, 0));
    }

    void testCanonicalRespawnResources()
    {
        CombatResolver resolver;
        const CombatantId player{ CombatantKind::Player, 7, {} };
        EXPECT(!resolver.prepareRespawn(player));
        auto dead = state(80, {});
        dead.health = 0;
        dead.alive = false;
        dead.fatigue = 0;
        dead.fatigueRatio = 0;
        EXPECT(resolver.upsert(player, dead));
        const auto restored = resolver.prepareRespawn(player);
        EXPECT(restored.has_value());
        EXPECT(resolver.find(player)->health == 0); // preparation is not authorization
        EXPECT(restored->health == 80 && restored->alive);
        EXPECT(restored->fatigue == 100 && restored->fatigueRatio == 1);
        EXPECT(resolver.upsert(player, *restored));
        EXPECT(!resolver.prepareRespawn(player)); // cannot heal a living player
        dead.maximumHealth = 0;
        EXPECT(resolver.upsert(player, dead));
        EXPECT(!resolver.prepareRespawn(player));
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

    void testUnarmedFatigueKnockoutAndRecovery()
    {
        CombatResolver resolver;
        const CombatantId attacker{ CombatantKind::Player, 7, {} };
        const CombatantId target{ CombatantKind::Actor, 11, "Balmora" };
        auto fist = state(100, {});
        fist.unarmed = true;
        fist.unarmedHealthMultiplier = 0.1;
        auto victim = state(100, { 64, 0, 0 });
        victim.fatigue = 5;
        victim.fatigueRatio = 0.05;
        victim.armorRating = 1000; // armor does not absorb hand-to-hand fatigue
        victim.fatigueRecoveryPerSecond = 2;
        EXPECT(resolver.upsert(attacker, fist));
        EXPECT(resolver.upsert(target, victim));
        EXPECT(resolver.fatigueRecovery(1).empty());
        const auto miss = resolver.resolve({ attacker, target, 1, AttackKind::Melee, 0.5 }, 0.9);
        EXPECT(miss.damage == 0 && miss.targetFatigue == 5 && miss.targetHealth == 100);
        const auto hit = resolver.resolve({ attacker, target, 2, AttackKind::Melee, 0.5 }, 0);
        EXPECT(hit.applied() && !hit.healthDamage && hit.damage == 20);
        EXPECT(hit.targetHealth == 100 && hit.targetFatigue == -15 && hit.targetKnockedOut);
        EXPECT(resolver.find(target)->fatigueRatio == 0);
        const auto healthHit = resolver.resolve({ attacker, target, 3, AttackKind::Melee, 0.5 }, 0);
        EXPECT(healthHit.healthDamage && healthHit.damage == 2);
        EXPECT(healthHit.targetHealth == 98 && healthHit.targetFatigue == -15);
        const auto recovery = resolver.fatigueRecovery(8);
        EXPECT(recovery.size() == 1 && recovery.front().first == target && recovery.front().second == 16);
        auto restored = *resolver.find(target);
        restored.fatigue += recovery.front().second;
        restored.fatigueRatio = restored.fatigue / restored.maximumFatigue;
        EXPECT(resolver.upsert(target, restored));
        const auto awake = resolver.resolve({ attacker, target, 4, AttackKind::Melee, 0 }, 0);
        EXPECT(!awake.healthDamage && awake.targetHealth == 98 && awake.targetFatigue == -9);
        EXPECT(resolver.fatigueRecovery(1000).front().second == 109); // capped at maximum
        EXPECT(resolver.fatigueRecovery(-1).empty());
        EXPECT(resolver.fatigueRecovery(std::numeric_limits<double>::quiet_NaN()).empty());

        victim.paralyzed = true;
        EXPECT(resolver.upsert(target, victim));
        const auto paralyzed = resolver.resolve({ attacker, target, 5, AttackKind::Melee, 1 }, 0);
        EXPECT(paralyzed.healthDamage && paralyzed.damage == 3 && paralyzed.targetFatigue == 5);
        // Ranged and equipped attacks retain the health path even with positive fatigue.
        const auto ranged = resolver.resolve({ attacker, target, 6, AttackKind::Ranged, 1 }, 0);
        EXPECT(ranged.healthDamage && ranged.targetFatigue == 5);
        fist.unarmed = false;
        EXPECT(resolver.upsert(attacker, fist));
        victim.paralyzed = false;
        EXPECT(resolver.upsert(target, victim));
        EXPECT(resolver.resolve({ attacker, target, 7, AttackKind::Melee, 1 }, 0).healthDamage);
        victim.health = 0;
        victim.alive = false;
        victim.recoveringFatigue = true;
        EXPECT(resolver.upsert(target, victim));
        EXPECT(resolver.fatigueRecovery(1).empty());
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
        invalid = state(10, {});
        invalid.fatigue = 25;
        invalid.fatigueRatio = 1;
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
    testAttackAnimationPhases();
    testCanonicalRespawnResources();
    testCanonicalHitAndDeath();
    testMissRangeAndSequence();
    testUnarmedFatigueKnockoutAndRecovery();
    testInvalidDataAndCapacity();
    testActorIdentityIsCellScoped();
    testActorRelocationIsAtomic();
    return sFailures;
}
