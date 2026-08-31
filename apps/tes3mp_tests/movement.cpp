#include <components/openmw-mp/Mechanics/MovementValidator.hpp>

#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <string>

namespace
{
    using namespace mwmp::mechanics;
    using namespace std::chrono_literals;

    int sFailures = 0;

    void expect(bool condition, const char* expression, int line)
    {
        if (condition)
            return;
        std::cerr << "movement.cpp:" << line << ": expectation failed: " << expression << '\n';
        ++sFailures;
    }

#define EXPECT(condition) expect((condition), #condition, __LINE__)

    void testSpeedBoundAndSequences()
    {
        MovementValidator validator;
        const auto start = MovementValidator::Clock::time_point{};
        EXPECT(validator.validate(7, { { 0, 0, 0 }, "Balmora", 10 }, 200, start).accepted());

        const auto accepted = validator.validate(
            7, { { 375, 0, 0 }, "Balmora", 11 }, 200, start + 1s);
        EXPECT(accepted.decision == MovementDecision::Accepted);
        EXPECT(accepted.allowedDistance == 375);

        const auto rejected = validator.validate(
            7, { { 751, 0, 0 }, "Balmora", 12 }, 200, start + 2s);
        EXPECT(rejected.decision == MovementDecision::SpeedExceeded);
        EXPECT(rejected.distance == 376);
        EXPECT(rejected.allowedDistance == 375);
        EXPECT(validator.current(7)->sequence == 11);

        EXPECT(validator.validate(7, { { 376, 0, 0 }, "Balmora", 11 }, 200,
                   start + 2s).decision
            == MovementDecision::StaleSequence);
        EXPECT(validator.validate(7, { { 376, 0, 0 }, "Balmora", 0 }, 200,
                   start + 2s).decision
            == MovementDecision::InvalidSequence);
    }

    void testTransitionsAreExplicitAndSingleUse()
    {
        MovementValidator validator;
        const auto start = MovementValidator::Clock::time_point{};
        EXPECT(validator.validate(1, { { 1, 2, 3 }, "Seyda Neen", 1 }, 100, start).accepted());
        EXPECT(validator.validate(1, { { 0, 0, 0 }, "Balmora", 2 }, 100,
                   start + 1s).decision
            == MovementDecision::TransitionNotAuthorized);

        EXPECT(validator.authorizeTransition(1, "Balmora", { 100, 200, 300 }, 8,
            start + 1s));
        EXPECT(validator.validate(1, { { 109, 200, 300 }, "Balmora", 2 }, 100,
                   start + 1s).decision
            == MovementDecision::TransitionNotAuthorized);
        EXPECT(validator.validate(1, { { 106, 200, 300 }, "Balmora", 3 }, 100,
                   start + 1s).decision
            == MovementDecision::AcceptedTransition);
        EXPECT(validator.validate(1, { { 0, 0, 0 }, "Vivec", 4 }, 100,
                   start + 2s).decision
            == MovementDecision::TransitionNotAuthorized);

        EXPECT(validator.authorizeTransition(1, "Vivec", { 0, 0, 0 }, 1, start + 2s));
        EXPECT(validator.validate(1, { { 0, 0, 0 }, "Vivec", 5 }, 100,
                   start + 8s).decision
            == MovementDecision::TransitionNotAuthorized);
    }

    void testInvalidInputCapacityAndCleanup()
    {
        MovementValidator validator(1);
        const auto now = MovementValidator::Clock::time_point{};
        EXPECT(validator.validate(0, { { 0, 0, 0 }, "Caldera", 1 }, 1, now).decision
            == MovementDecision::InvalidConnection);
        EXPECT(validator.validate(1,
                   { { std::numeric_limits<double>::quiet_NaN(), 0, 0 }, "Caldera", 1 },
                   1, now).decision
            == MovementDecision::InvalidCoordinate);
        EXPECT(validator.validate(1, { { 0, 0, 0 }, "Caldera", 1 }, -1, now).decision
            == MovementDecision::InvalidSpeed);
        EXPECT(validator.validate(1, { { 0, 0, 0 }, "Caldera", 1 }, 1, now).accepted());
        EXPECT(validator.validate(2, { { 0, 0, 0 }, "Pelagiad", 1 }, 1, now).decision
            == MovementDecision::CapacityReached);
        EXPECT(validator.size() == 1);
        EXPECT(validator.erase(1));
        EXPECT(!validator.erase(1));
        EXPECT(validator.size() == 0);
        EXPECT(std::string(describe(MovementDecision::SpeedExceeded))
            == "the movement exceeds the theoretical speed bound");
    }
}

int runMovementTests()
{
    testSpeedBoundAndSequences();
    testTransitionsAreExplicitAndSingleUse();
    testInvalidInputCapacityAndCleanup();
    return sFailures;
}
