#include <components/openmw-mp/Mechanics/CastIntentValidator.hpp>

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
        std::cerr << "cast.cpp:" << line << ": expectation failed: " << expression << '\n';
        ++sFailures;
    }

#define EXPECT(condition) expect((condition), #condition, __LINE__)

    CastIntent regularCast()
    {
        CastIntent intent;
        intent.caster = { CombatantKind::Player, 7, {} };
        intent.target = CombatantId{ CombatantKind::Actor, 19, "Balmora" };
        intent.kind = CastKind::Regular;
        intent.sourceId = "fireball";
        intent.projectile = ProjectileIntent{
            { 1, 2, 3 }, { 0, 0, 0, 1 } };
        return intent;
    }

    void testAcceptedIntents()
    {
        CastIntentValidator validator;
        EXPECT(validator.validate(regularCast()) == CastIntentDecision::Accepted);

        CastIntent item = regularCast();
        item.kind = CastKind::Item;
        item.sourceId = "ring_of_fire";
        item.target.reset();
        item.projectile.reset();
        EXPECT(validator.validate(item) == CastIntentDecision::Accepted);
    }

    void testIdentityValidation()
    {
        CastIntentValidator validator;
        CastIntent intent = regularCast();
        intent.caster.value = 0;
        EXPECT(validator.validate(intent) == CastIntentDecision::InvalidCaster);

        intent = regularCast();
        intent.target->value = 0;
        EXPECT(validator.validate(intent) == CastIntentDecision::InvalidTarget);

        intent.caster = { CombatantKind::Actor, 5, "Seyda Neen" };
        intent.target = CombatantId{ CombatantKind::Actor, 6, "Balmora" };
        EXPECT(validator.validate(intent) == CastIntentDecision::InvalidTarget);
    }

    void testSourceValidation()
    {
        CastIntentValidator validator;
        CastIntent intent = regularCast();
        intent.sourceId.clear();
        EXPECT(validator.validate(intent) == CastIntentDecision::InvalidSource);

        intent.sourceId.assign(CastIntentValidator::MaximumSourceIdBytes + 1, 'x');
        EXPECT(validator.validate(intent) == CastIntentDecision::InvalidSource);

        intent.sourceId = std::string("spell\0suffix", 12);
        EXPECT(validator.validate(intent) == CastIntentDecision::InvalidSource);
    }

    void testProjectileValidation()
    {
        CastIntentValidator validator;
        CastIntent intent = regularCast();
        intent.projectile->origin.x = std::numeric_limits<double>::quiet_NaN();
        EXPECT(validator.validate(intent) == CastIntentDecision::InvalidProjectile);

        intent = regularCast();
        intent.projectile->orientation = { 0, 0, 0, 0 };
        EXPECT(validator.validate(intent) == CastIntentDecision::InvalidProjectile);

        intent.projectile->orientation = { 0, 0, 0, 3 };
        EXPECT(validator.validate(intent) == CastIntentDecision::InvalidProjectile);
        EXPECT(std::string(describe(CastIntentDecision::InvalidProjectile))
            == "the cast intent has invalid projectile geometry");
    }

    void testReportedCasterTransform()
    {
        CastIntentValidator validator;
        CastIntent intent = regularCast();
        intent.reportedCasterTransform = TransformIntent{ { 10, 20, 30 }, {} };
        intent.canonicalCasterTransform = TransformIntent{ { 11, 20, 30 }, {} };
        intent.reportedCasterDirection = TransformIntent{ { 0, 1, 0 }, {} };
        EXPECT(validator.validate(intent) == CastIntentDecision::Accepted);

        intent.reportedCasterTransform->translation.x = 1000;
        EXPECT(validator.validate(intent) == CastIntentDecision::InvalidCasterTransform);

        intent = regularCast();
        intent.reportedCasterTransform = TransformIntent{};
        EXPECT(validator.validate(intent) == CastIntentDecision::InvalidCasterTransform);

        intent.canonicalCasterTransform = TransformIntent{};
        intent.reportedCasterDirection = TransformIntent{};
        intent.reportedCasterDirection->rotation.z
            = std::numeric_limits<double>::infinity();
        EXPECT(validator.validate(intent) == CastIntentDecision::InvalidCasterTransform);
    }
}

int runCastTests()
{
    testAcceptedIntents();
    testIdentityValidation();
    testSourceValidation();
    testProjectileValidation();
    testReportedCasterTransform();
    return sFailures;
}
