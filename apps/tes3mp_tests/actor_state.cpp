#include <components/openmw-mp/Mechanics/ActorStateLedger.hpp>

#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace
{
    using namespace mwmp::mechanics;

    int sFailures = 0;

    void expect(bool condition, const char* expression, int line)
    {
        if (condition)
            return;
        std::cerr << "actor_state.cpp:" << line << ": expectation failed: "
                  << expression << '\n';
        ++sFailures;
    }

#define EXPECT(condition) expect((condition), #condition, __LINE__)

    ActorEquipmentUpdate actor(std::string cell, std::uint32_t refNum,
        std::uint32_t mpNum, std::string item = {})
    {
        ActorEquipmentUpdate result;
        result.identity = { std::move(cell), refNum, mpNum };
        if (!item.empty())
            result.equipment[0] = { std::move(item), 1, -1, -1 };
        return result;
    }

    void testAtomicEquipmentUpdates()
    {
        ActorStateLedger ledger;
        const std::vector<ActorEquipmentUpdate> updates{
            actor("Balmora", 1, 0, "iron_sword"),
            actor("Balmora", 2, 0, "iron_cuirass"),
        };
        EXPECT(ledger.previewEquipment(updates).applied());
        EXPECT(ledger.size() == 0);
        EXPECT(ledger.applyEquipment(updates).applied());
        EXPECT(ledger.size() == 2);
        EXPECT(ledger.equipment({ "Balmora", 1, 0 })->at(0).refId
            == "iron_sword");

        ActorEquipmentUpdate invalid = actor("Balmora", 3, 0, "robe");
        invalid.equipment[0].enchantmentCharge
            = std::numeric_limits<double>::quiet_NaN();
        EXPECT(ledger.applyEquipment(
                   { actor("Balmora", 1, 0, "steel_sword"), invalid }).decision
            == ActorStateDecision::InvalidEquipment);
        EXPECT(ledger.equipment({ "Balmora", 1, 0 })->at(0).refId
            == "iron_sword");
    }

    void testIdentityAndLimits()
    {
        ActorStateLedger ledger(1);
        EXPECT(ledger.previewEquipment({}).decision
            == ActorStateDecision::InvalidBatch);
        EXPECT(ledger.previewEquipment({ actor("", 1, 0) }).decision
            == ActorStateDecision::InvalidIdentity);
        EXPECT(ledger.previewEquipment({ actor("Balmora", 0, 0) }).decision
            == ActorStateDecision::InvalidIdentity);

        const auto first = actor("Balmora", 1, 0);
        EXPECT(ledger.applyEquipment({ first }).applied());
        EXPECT(ledger.previewEquipment({ first, first }).decision
            == ActorStateDecision::DuplicateActor);
        EXPECT(ledger.previewEquipment({ actor("Balmora", 2, 0) }).decision
            == ActorStateDecision::ActorLimitReached);
    }

    void testCleanup()
    {
        ActorStateLedger ledger;
        EXPECT(ledger.applyEquipment({ actor("Balmora", 1, 0),
                   actor("Ald-ruhn", 1, 0) }).applied());
        EXPECT(ledger.eraseCell("Balmora") == 1);
        EXPECT(!ledger.equipment({ "Balmora", 1, 0 }).has_value());
        EXPECT(ledger.size() == 1);
        ledger.clear();
        EXPECT(ledger.size() == 0);
        EXPECT(std::string(describe(ActorStateDecision::DuplicateActor))
            == "an actor occurs more than once in the batch");
    }
}

int runActorStateTests()
{
    testAtomicEquipmentUpdates();
    testIdentityAndLimits();
    testCleanup();
    return sFailures;
}
