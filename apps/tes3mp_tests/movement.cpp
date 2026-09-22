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

    void testCellTransitionIntentsAreStaged()
    {
        MovementValidator validator;
        const auto start = MovementValidator::Clock::time_point{};
        EXPECT(validator.validate(1, { { 10, 20, 30 }, "Balmora", 1 }, 100,
                   start).accepted());
        EXPECT(validator.previewCellTransition(1, "Vivec", { 200, 20, 30 }, 64)
                   .decision == MovementDecision::SpeedExceeded);
        EXPECT(validator.validate(1, { {}, "Vivec", 2 }, 100, start + 1s)
                   .decision == MovementDecision::TransitionNotAuthorized);

        EXPECT(validator.previewCellTransition(1, "Vivec", { 11, 20, 30 }, 64)
                   .decision == MovementDecision::AcceptedTransition);
        EXPECT(validator.acceptCellTransition(1, "Vivec", { 11, 20, 30 }, 64,
                   start + 1s).accepted());
        EXPECT(validator.validate(1, { { 5000, 6000, 7000 }, "Vivec", 3 }, 100,
                   start + 1s).decision == MovementDecision::AcceptedTransition);
        EXPECT(validator.previewCellTransition(1, "Vivec", { 5000, 6000, 7000 }, 64)
                   .decision == MovementDecision::InvalidTransition);

        EXPECT(validator.authorizeTransition(1, "Seyda Neen", { 1, 2, 3 }, 4,
            start + 2s));
        EXPECT(validator.acceptCellTransition(1, "Seyda Neen",
                   { 5000, 6000, 7000 }, 64, start + 2s).accepted());
        EXPECT(validator.validate(1, { { 100, 2, 3 }, "Seyda Neen", 4 }, 100,
                   start + 2s).decision == MovementDecision::TransitionNotAuthorized);
        EXPECT(validator.validate(1, { { 4, 2, 3 }, "Seyda Neen", 5 }, 100,
                   start + 2s).decision == MovementDecision::AcceptedTransition);

        MovementValidator initial;
        EXPECT(initial.acceptCellTransition(2, "Caldera", {}, 64, start).accepted());
        EXPECT(initial.validate(2, { {}, "Balmora", 1 }, 100, start)
                   .decision == MovementDecision::TransitionNotAuthorized);
        EXPECT(initial.validate(2, { {}, "Caldera", 2 }, 100, start)
                   .decision == MovementDecision::AcceptedTransition);
    }

    void testDoorwayTravelBetweenSnapshots()
    {
        MovementValidator validator;
        const auto start = MovementValidator::Clock::time_point{};
        EXPECT(validator.validate(1, { {}, "Interior", 1 }, 600, start).accepted());
        // Walking to a door between snapshots exceeds the old fixed 128-unit
        // tolerance but remains inside the normal time-and-speed bound.
        EXPECT(validator.previewCellTransition(1, "Exterior", { 228, 0, 0 }, 128,
            start + 100ms, 600).accepted());
        EXPECT(validator.previewCellTransition(1, "Exterior", { 2000, 0, 0 }, 128,
            start + 100ms, 600).decision == MovementDecision::SpeedExceeded);
        EXPECT(validator.previewCellTransition(1, "Exterior", {}, 128,
            start, -1).decision == MovementDecision::InvalidTransition);
        EXPECT(validator.acceptCellTransition(1, "Exterior", { 228, 0, 0 }, 128,
            start + 100ms, MovementValidator::TransitionLifetime, 600).accepted());
        EXPECT(validator.validate(1, { { 72000, 0, 0 }, "Exterior", 2 }, 600,
            start + 100ms).decision == MovementDecision::AcceptedTransition);
        EXPECT(validator.validate(1, { {}, "Interior", 3 }, 600,
            start + 200ms).decision == MovementDecision::TransitionNotAuthorized);
    }

    void testRejectedAtomicTransitionPreservesState()
    {
        MovementValidator validator;
        const auto start = MovementValidator::Clock::time_point{};
        EXPECT(validator.validate(1, { {}, "Interior", 1 }, 600, start).accepted());
        EXPECT(validator.authorizeTransition(1, "Exterior", { 72000, 0, 0 }, 128, start));
        EXPECT(!validator.commitCellTransition(1, { {}, "Exterior", 2 }, {}, 128,
            600, start + 100ms).accepted());
        EXPECT(validator.current(1)->cell == "Interior");
        EXPECT(validator.current(1)->sequence == 1);
        EXPECT(validator.commitCellTransition(1, { { 72000, 0, 0 }, "Exterior", 3 },
            {}, 128, 600, start + 200ms).accepted());
        EXPECT(validator.current(1)->cell == "Exterior");
        EXPECT(validator.validate(1, { {}, "Interior", 4 }, 600, start + 300ms)
            .decision == MovementDecision::TransitionNotAuthorized);
    }

    void testRepeatedServerTeleportAcknowledgements()
    {
        MovementValidator validator;
        const auto start = MovementValidator::Clock::time_point{};
        EXPECT(validator.validate(1, { {}, "Exterior", 1 }, 600, start).accepted());
        // Applying a server cell packet can update the client's cached position
        // before it sends the acknowledgement. The authorized destination, not
        // that old-position field, establishes the teleport's spatial bound.
        for (std::uint64_t cycle = 0; cycle < 6; ++cycle)
        {
            const auto now = start + std::chrono::seconds(cycle + 1);
            const Position3 position{ cycle % 2 == 0 ? 28000.0 : 0.0, 0, 0 };
            const std::string cell = cycle % 2 == 0 ? "Temple" : "Exterior";
            EXPECT(validator.authorizeTransition(1, cell, position, 128, now));
            const auto previous = validator.current(1);
            EXPECT(validator.previewAuthorizedTransition(1,
                { position, cell, cycle + 2 }, now).accepted());
            EXPECT(validator.current(1)->sequence == previous->sequence);
            EXPECT(!validator.previewAuthorizedTransition(1,
                { { position.x + 129, 0, 0 }, cell, cycle + 2 }, now).accepted());
            EXPECT(!validator.previewAuthorizedTransition(1,
                { position, "Other cell", cycle + 2 }, now).accepted());
            EXPECT(!validator.previewAuthorizedTransition(1,
                { position, cell, cycle + 2 }, now + 6s).accepted());
            EXPECT(validator.commitCellTransition(1, { position, cell, cycle + 2 },
                position, 128, 600, now).decision == MovementDecision::AcceptedTransition);
            EXPECT(validator.current(1)->cell == cell);
            EXPECT(validator.current(1)->position == position);
            EXPECT(!validator.previewAuthorizedTransition(1,
                { position, cell, cycle + 3 }, now).accepted());
        }

        // If the position snapshot arrives first, it consumes the same grant.
        const auto now = start + 7s;
        EXPECT(validator.authorizeTransition(1, "Temple", { 28000, 0, 0 }, 128, now));
        EXPECT(validator.validate(1, { { 28000, 0, 0 }, "Temple", 8 }, 600, now)
            .decision == MovementDecision::AcceptedTransition);
        EXPECT(!validator.previewAuthorizedTransition(1,
            { { 28000, 0, 0 }, "Temple", 9 }, now).accepted());
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
    testCellTransitionIntentsAreStaged();
    testDoorwayTravelBetweenSnapshots();
    testRejectedAtomicTransitionPreservesState();
    testRepeatedServerTeleportAcknowledgements();
    testInvalidInputCapacityAndCleanup();
    return sFailures;
}
