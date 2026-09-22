#include <components/openmw-mp/Mechanics/ActorStateLedger.hpp>
#include <components/openmw-mp/Mechanics/ActorRecovery.hpp>

#include <chrono>
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

    void testActorRecovery()
    {
        using namespace std::chrono_literals;
        ActorRecovery recovery;
        const ActorIdentity id{"Balmora", 1, 0};
        ActorRecoveryAnchor anchor{id, "guard", {}};
        anchor.transform.position = {1000, 0, 100};
        EXPECT(recovery.install({anchor}));
        const auto start = ActorRecovery::Clock::time_point{};
        const Position3 stuck{0, 0, 100};
        // Idle, ordinary travel, and another reference never build a stall window.
        for (int i = 0; i < 30; ++i)
        {
            EXPECT(!recovery.observe(id, "guard", stuck, false, 1, start + i * 1s));
            EXPECT(!recovery.observe({"other cell", 1, 0}, "guard", stuck, true, 1, start + i * 1s));
        }
        EXPECT(recovery.size() == 1);
        recovery.forget(id);
        for (int i = 0; i < 30; ++i)
            EXPECT(!recovery.observe(id, "guard", {i * 100.0, 0, 100}, true, 1, start + i * 1s));
        recovery.forget(id);
        for (int i = 0; i < 20; ++i)
            EXPECT(!recovery.observe(id, "guard", {double(i % 2) * 10, 0, 100}, true, 1, start + i * 1s));
        auto destination = recovery.observe(id, "guard", stuck, true, 1, start + 20s);
        EXPECT(destination && destination->position.x == 1000);
        EXPECT(recovery.pending(id, stuck, 1, start + 21s).has_value());
        EXPECT(!recovery.pending(id, stuck, 2, start + 21s));
        EXPECT(!recovery.pending(id, anchor.transform.position, 1, start + 21s));
        EXPECT(!recovery.pending(id, stuck, 1, start + 25s));
        EXPECT(!recovery.observe(id, "guard", anchor.transform.position, false, 1, start + 21s));
        for (int i = 22; i < 140; ++i)
            EXPECT(!recovery.observe(id, "guard", stuck, true, 1, start + i * 1s));
        for (int cycle = 1; cycle < 3; ++cycle)
        {
            const int base = cycle == 1 ? 140 : 300;
            for (int i = base; i < base + 20; ++i)
                EXPECT(!recovery.observe(id, "guard", stuck, true, 1, start + i * 1s));
            EXPECT(recovery.observe(id, "guard", stuck, true, 1, start + (base + 20) * 1s).has_value());
        }
        for (int i = 450; i < 510; ++i)
            EXPECT(!recovery.observe(id, "guard", stuck, true, 1, start + i * 1s));
        recovery.forget(id);
        // Long receive gaps and a new simulation authority restart the timer.
        for (int i = 0; i < 20; ++i)
            EXPECT(!recovery.observe(id, "guard", stuck, true, 1, start + i * 1s));
        EXPECT(!recovery.observe(id, "guard", stuck, true, 1, start + 30s));
        for (int i = 31; i < 49; ++i)
            EXPECT(!recovery.observe(id, "guard", stuck, true, 1, start + i * 1s));
        EXPECT(!recovery.observe(id, "guard", stuck, true, 2, start + 49s));
        EXPECT(!recovery.observe(id, "other npc", stuck, true, 2, start + 70s));
        recovery.forget(id);
        for (int i = 0; i < 30; ++i)
            EXPECT(!recovery.observe(id, "guard", {-5000, 0, 100}, true, 1, start + i * 1s));
        EXPECT(!recovery.install({anchor, anchor}));
        anchor.transform.rotation.x = std::numeric_limits<double>::quiet_NaN();
        EXPECT(!recovery.install({anchor}));
    }

    void testRecoveryMovementBaseline()
    {
        using namespace std::chrono_literals;
        ActorStateLedger ledger;
        ActorPositionUpdate update;
        update.identity = {"Balmora", 1, 0};
        update.sequence = 10;
        auto now = ActorStateLedger::Clock::now();
        EXPECT(ledger.applyPositions({update}, 100, now).applied());
        ActorTransform anchor;
        anchor.position.x = 1000;
        EXPECT(!ledger.recoverPosition({"Balmora", 2, 0}, anchor, now));
        EXPECT(ledger.recoverPosition(update.identity, anchor, now));
        update.transform = anchor;
        EXPECT(ledger.previewPositions({update}, 100, now).decision == ActorStateDecision::StaleSequence);
        update.sequence = 11;
        EXPECT(ledger.applyPositions({update}, 100, now).applied());
        update.sequence = 12;
        update.transform.position.x = 0;
        EXPECT(!ledger.previewPositions({update}, 100, now).applied());
        update.transform.position.x = 1010;
        EXPECT(ledger.previewPositions({update}, 100, now + 100ms).applied());
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

    ActorPositionUpdate position(const char* cell, std::uint32_t refNum,
        double x, std::uint64_t sequence)
    {
        ActorPositionUpdate update;
        update.identity = { cell, refNum, 0 };
        update.transform.position.x = x;
        update.sequence = sequence;
        return update;
    }

    void testAtomicPositionUpdates()
    {
        using namespace std::chrono_literals;
        ActorStateLedger ledger;
        const auto start = ActorStateLedger::Clock::time_point{};
        EXPECT(ledger.previewPositions({ position("Balmora", 1, 0, 1) }, 200, start)
            .applied());
        EXPECT(!ledger.position({ "Balmora", 1, 0 }).has_value());
        EXPECT(ledger.applyPositions({ position("Balmora", 1, 0, 1) }, 200, start)
            .applied());

        EXPECT(ledger.applyPositions({ position("Balmora", 1, 375, 2) }, 200,
                   start + 1s).applied());
        EXPECT(ledger.position({ "Balmora", 1, 0 })->position.x == 375);
        EXPECT(ledger.previewPositions({ position("Balmora", 1, 751, 3) }, 200,
                   start + 2s).decision == ActorStateDecision::SpeedExceeded);
        EXPECT(ledger.previewPositions({ position("Balmora", 1, 375, 2) }, 200,
                   start + 2s).decision == ActorStateDecision::StaleSequence);

        auto invalid = position("Balmora", 2, 0, 3);
        invalid.transform.direction.x = std::numeric_limits<double>::quiet_NaN();
        EXPECT(ledger.applyPositions({ position("Balmora", 1, 376, 3), invalid },
                   200, start + 2s).decision == ActorStateDecision::InvalidPosition);
        EXPECT(ledger.position({ "Balmora", 1, 0 })->position.x == 375);
        EXPECT(ledger.previewPositions({ position("Balmora", 1, 376, 0) }, 200,
                   start + 2s).decision == ActorStateDecision::InvalidSequence);
        EXPECT(ledger.previewPositions({ position("Balmora", 1, 376, 3) }, -1,
                   start + 2s).decision == ActorStateDecision::InvalidSpeed);
    }

    ActorRosterUpdate rosterActor(const char* cell, std::uint32_t refNum,
        const char* refId)
    {
        return { { cell, refNum, 0 }, refId };
    }

    void testAtomicRosterUpdates()
    {
        ActorStateLedger ledger(2);
        const std::vector<ActorRosterUpdate> initial{
            rosterActor("Balmora", 1, "guard"),
            rosterActor("Balmora", 2, "rat"),
        };
        EXPECT(ledger.previewRoster(ActorRosterAction::Set, "Balmora", initial)
            .applied());
        EXPECT(ledger.size() == 0);
        EXPECT(ledger.applyRoster(ActorRosterAction::Set, "Balmora", initial)
            .applied());
        EXPECT(ledger.contains({ "Balmora", 1, 0 }));
        EXPECT(ledger.identities("Balmora").size() == 2);
        EXPECT(*ledger.refId({ "Balmora", 1, 0 }) == "guard");
        EXPECT(ledger.previewRoster(ActorRosterAction::Add, "Balmora",
                   { rosterActor("Balmora", 3, "scrib") }).decision
            == ActorStateDecision::ActorLimitReached);
        EXPECT(ledger.previewRoster(ActorRosterAction::Add, "Balmora",
                   { rosterActor("Ald-ruhn", 3, "scrib") }).decision
            == ActorStateDecision::InvalidIdentity);
        EXPECT(ledger.previewRoster(ActorRosterAction::Add, "Balmora",
                   { rosterActor("Balmora", 1, "") }).decision
            == ActorStateDecision::InvalidRefId);
        EXPECT(ledger.previewRoster(ActorRosterAction::Remove, "Balmora",
                   { rosterActor("Balmora", 3, "") }).decision
            == ActorStateDecision::UnknownActor);

        EXPECT(ledger.applyRoster(ActorRosterAction::Remove, "Balmora",
                   { rosterActor("Balmora", 2, "") }).applied());
        EXPECT(!ledger.refId({ "Balmora", 2, 0 }));
        EXPECT(ledger.applyRoster(ActorRosterAction::Add, "Balmora",
                   { rosterActor("Balmora", 3, "scrib") }).applied());
        EXPECT(ledger.applyRoster(ActorRosterAction::Set, "Balmora",
                   { rosterActor("Balmora", 3, "kwama") }).applied());
        EXPECT(ledger.size() == 1);
        EXPECT(!ledger.refId({ "Balmora", 1, 0 }));
        EXPECT(*ledger.refId({ "Balmora", 3, 0 }) == "kwama");
        EXPECT(ledger.applyRoster(ActorRosterAction::Set, "Balmora", {}).applied());
        EXPECT(ledger.size() == 0);
    }

    ActorCellChangeUpdate cellChange(const char* sourceCell,
        const char* destinationCell, std::uint32_t refNum, double x,
        std::uint64_t sequence)
    {
        ActorCellChangeUpdate update;
        update.source = { sourceCell, refNum, 0 };
        update.destinationCell = destinationCell;
        update.transform.position.x = x;
        update.sequence = sequence;
        return update;
    }

    void testAtomicCellChanges()
    {
        const auto now = ActorStateLedger::Clock::time_point{};
        ActorStateLedger ledger;
        EXPECT(ledger.applyRoster(ActorRosterAction::Add, "Balmora",
                   { rosterActor("Balmora", 1, "guard"),
                       rosterActor("Balmora", 2, "rat"),
                       rosterActor("Balmora", 3, "scrib") }).applied());
        EXPECT(ledger.applyEquipment({ actor("Balmora", 1, 0, "iron_sword") })
            .applied());

        const auto guard = cellChange("Balmora", "Ald-ruhn", 1, 250, 4);
        EXPECT(ledger.previewCellChanges({ guard }).applied());
        EXPECT(ledger.contains({ "Balmora", 1, 0 }));
        EXPECT(!ledger.contains({ "Ald-ruhn", 1, 0 }));
        EXPECT(ledger.applyCellChanges({ guard }, now).applied());
        EXPECT(!ledger.contains({ "Balmora", 1, 0 }));
        EXPECT(ledger.contains({ "Ald-ruhn", 1, 0 }));
        EXPECT(*ledger.refId({ "Ald-ruhn", 1, 0 }) == "guard");
        EXPECT(ledger.equipment({ "Ald-ruhn", 1, 0 })->at(0).refId
            == "iron_sword");
        EXPECT(ledger.position({ "Ald-ruhn", 1, 0 })->position.x == 250);

        EXPECT(ledger.previewCellChanges({}).decision
            == ActorStateDecision::InvalidBatch);
        EXPECT(ledger.previewCellChanges(
                   { cellChange("Balmora", "Balmora", 2, 0, 5) }).decision
            == ActorStateDecision::InvalidIdentity);
        EXPECT(ledger.previewCellChanges(
                   { cellChange("Balmora", "Vivec", 99, 0, 5) }).decision
            == ActorStateDecision::UnknownActor);

        EXPECT(ledger.applyRoster(ActorRosterAction::Add, "Ald-ruhn",
                   { rosterActor("Ald-ruhn", 2, "rat") }).applied());
        const auto rat = cellChange("Balmora", "Ald-ruhn", 2, 100, 5);
        EXPECT(ledger.previewCellChanges({ rat }).decision
            == ActorStateDecision::DestinationOccupied);
        EXPECT(ledger.contains({ "Balmora", 2, 0 }));

        ActorCellChangeUpdate invalid
            = cellChange("Balmora", "Vivec", 3, 0, 5);
        invalid.transform.rotation.z = std::numeric_limits<double>::infinity();
        EXPECT(ledger.applyCellChanges(
                   { cellChange("Balmora", "Seyda Neen", 2, 50, 5), invalid },
                   now).decision == ActorStateDecision::InvalidPosition);
        EXPECT(ledger.contains({ "Balmora", 2, 0 }));
        EXPECT(!ledger.contains({ "Seyda Neen", 2, 0 }));
    }

    void testAtomicAiUpdates()
    {
        ActorStateLedger ledger;
        EXPECT(ledger.applyRoster(ActorRosterAction::Add, "Balmora",
                   { rosterActor("Balmora", 1, "guard"),
                       rosterActor("Balmora", 2, "rat") }).applied());

        ActorAiUpdate combat;
        combat.identity = { "Balmora", 1, 0 };
        combat.state.action = ActorAiAction::Combat;
        combat.state.target = ActorAiTarget{ ActorAiTargetKind::Reference, 0,
            { "Balmora", 2, 0 } };
        EXPECT(ledger.previewAi({ combat }).applied());
        EXPECT(!ledger.ai(combat.identity));
        EXPECT(ledger.applyAi({ combat }).applied());
        EXPECT(ledger.ai(combat.identity)->action == ActorAiAction::Combat);

        ActorAiUpdate invalid = combat;
        invalid.identity.refNum = 2;
        invalid.state.coordinates.x = std::numeric_limits<double>::quiet_NaN();
        EXPECT(ledger.applyAi({ invalid }).decision
            == ActorStateDecision::InvalidAiState);
        EXPECT(!ledger.ai(invalid.identity));

        invalid = combat;
        invalid.state.target->reference.cell = "Seyda Neen";
        EXPECT(ledger.previewAi({ invalid }).decision
            == ActorStateDecision::InvalidAiTarget);
        invalid = combat;
        invalid.state.target->reference = { "Balmora", 0, 0 };
        EXPECT(ledger.previewAi({ invalid }).decision
            == ActorStateDecision::InvalidAiTarget);
        invalid = combat;
        invalid.identity.refNum = 99;
        EXPECT(ledger.previewAi({ invalid }).decision
            == ActorStateDecision::UnknownActor);
    }
}

int runActorStateTests()
{
    testActorRecovery();
    testRecoveryMovementBaseline();
    testAtomicEquipmentUpdates();
    testIdentityAndLimits();
    testCleanup();
    testAtomicPositionUpdates();
    testAtomicRosterUpdates();
    testAtomicCellChanges();
    testAtomicAiUpdates();
    return sFailures;
}
