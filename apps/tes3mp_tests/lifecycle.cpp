#include <components/openmw-mp/Mechanics/PlayerLifecycle.hpp>

#include <iostream>
#include <string>

namespace
{
    using namespace mwmp::mechanics;

    int sFailures = 0;

    void expect(bool condition, const char* expression, int line)
    {
        if (condition)
            return;
        std::cerr << "lifecycle.cpp:" << line << ": expectation failed: " << expression << '\n';
        ++sFailures;
    }

#define EXPECT(condition) expect((condition), #condition, __LINE__)

    void testCanonicalLifecycle()
    {
        PlayerLifecycle lifecycle;
        EXPECT(lifecycle.state(7) == PlayerLifeState::Alive);

        const auto death = lifecycle.reportDeath(7);
        EXPECT(death.applied());
        EXPECT(death.state == PlayerLifeState::Dead);
        EXPECT(death.generation == 1);
        EXPECT(lifecycle.reportDeath(7).decision == PlayerLifeDecision::AlreadyDead);

        const auto respawning = lifecycle.beginRespawn(7, 2);
        EXPECT(respawning.applied());
        EXPECT(respawning.state == PlayerLifeState::Respawning);
        EXPECT(lifecycle.pendingRespawnType(7) == 2);
        EXPECT(lifecycle.beginRespawn(7, 2).decision == PlayerLifeDecision::NotDead);
        EXPECT(lifecycle.acknowledgeRespawn(7, 1).decision
            == PlayerLifeDecision::RespawnTypeMismatch);

        const auto alive = lifecycle.acknowledgeRespawn(7, 2);
        EXPECT(alive.applied());
        EXPECT(alive.state == PlayerLifeState::Alive);
        EXPECT(alive.generation == 1);
        EXPECT(!lifecycle.pendingRespawnType(7));
        EXPECT(lifecycle.acknowledgeRespawn(7, 2).decision
            == PlayerLifeDecision::NotRespawning);
    }

    void testInvalidTransitionsAndCapacity()
    {
        PlayerLifecycle lifecycle(1);
        EXPECT(lifecycle.reportDeath(0).decision == PlayerLifeDecision::InvalidConnection);
        EXPECT(lifecycle.beginRespawn(1, 3).decision
            == PlayerLifeDecision::InvalidRespawnType);
        EXPECT(lifecycle.beginRespawn(1, 0).decision == PlayerLifeDecision::NotDead);
        EXPECT(lifecycle.size() == 1);
        EXPECT(lifecycle.reportDeath(2).decision == PlayerLifeDecision::CapacityReached);
        EXPECT(lifecycle.erase(1));
        EXPECT(!lifecycle.erase(1));
        EXPECT(lifecycle.reportDeath(2).applied());
        EXPECT(std::string(describe(PlayerLifeDecision::RespawnTypeMismatch))
            == "the respawn acknowledgement does not match the server result");
    }
}

int runLifecycleTests()
{
    testCanonicalLifecycle();
    testInvalidTransitionsAndCapacity();
    return sFailures;
}
