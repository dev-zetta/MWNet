#include <components/openmw-mp/Mechanics/InventoryLedger.hpp>

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
        std::cerr << "inventory.cpp:" << line << ": expectation failed: "
                  << expression << '\n';
        ++sFailures;
    }

#define EXPECT(condition) expect((condition), #condition, __LINE__)

    InventoryItem item(std::string refId, std::int64_t count)
    {
        InventoryItem result;
        result.refId = std::move(refId);
        result.count = count;
        return result;
    }

    void testSetAddRemove()
    {
        InventoryLedger ledger;
        const InventoryOwner player{ InventoryOwnerKind::Player, 7 };
        EXPECT(ledger.apply(player, InventoryAction::Set,
                   { item("iron_sword", 1), item("gold_001", 20) }).applied());
        EXPECT(ledger.apply(player, InventoryAction::Add,
                   { item("gold_001", 5), item("p_restore_health_s", 2) }).applied());
        auto snapshot = ledger.snapshot(player);
        EXPECT(snapshot.has_value());
        EXPECT(snapshot->size() == 3);
        EXPECT(snapshot->at(1).count == 25);

        EXPECT(ledger.apply(player, InventoryAction::Remove,
                   { item("gold_001", 10) }).applied());
        snapshot = ledger.snapshot(player);
        EXPECT(snapshot->at(1).count == 15);
        EXPECT(ledger.apply(player, InventoryAction::Remove,
                   { item("iron_sword", 1) }).applied());
        EXPECT(ledger.snapshot(player)->size() == 2);
    }

    void testTransactionalFailure()
    {
        InventoryLedger ledger;
        const InventoryOwner player{ InventoryOwnerKind::Player, 1 };
        EXPECT(ledger.apply(player, InventoryAction::Set, { item("gold_001", 10) }).applied());
        const InventoryResult result = ledger.apply(player, InventoryAction::Remove,
            { item("gold_001", 5), item("missing", 1) });
        EXPECT(result.decision == InventoryDecision::MissingItem);
        EXPECT(ledger.snapshot(player)->at(0).count == 10);

        InventoryItem invalid = item("bad", 1);
        invalid.enchantmentCharge = std::numeric_limits<double>::quiet_NaN();
        EXPECT(ledger.apply(player, InventoryAction::Add, { invalid }).decision
            == InventoryDecision::InvalidItem);
        EXPECT(ledger.snapshot(player)->size() == 1);
    }

    void testPreviewDoesNotMutate()
    {
        InventoryLedger ledger;
        const InventoryOwner player{ InventoryOwnerKind::Player, 1 };
        EXPECT(ledger.apply(player, InventoryAction::Set,
                   { item("gold_001", 10) }).applied());

        const InventoryResult preview = ledger.preview(player, InventoryAction::Add,
            { item("gold_001", 5), item("p_restore_health_s", 2) });
        EXPECT(preview.applied());
        EXPECT(preview.stackCount == 2);

        const auto beforeCommit = ledger.snapshot(player);
        EXPECT(beforeCommit.has_value());
        EXPECT(beforeCommit->size() == 1);
        EXPECT(beforeCommit->front().count == 10);

        const InventoryResult rejected = ledger.preview(player, InventoryAction::Remove,
            { item("gold_001", 11) });
        EXPECT(rejected.decision == InventoryDecision::InsufficientItems);
        const auto afterReject = ledger.snapshot(player);
        EXPECT(afterReject.has_value());
        EXPECT(afterReject->front().count == 10);

        std::vector<InventoryItem> candidate;
        EXPECT(ledger.previewSnapshot(player, InventoryAction::Remove,
                   { item("gold_001", 4) }, candidate).applied());
        EXPECT(candidate.front().count == 6);
        EXPECT(ledger.snapshot(player)->front().count == 10);
    }

    void testAtomicTransfer()
    {
        InventoryLedger ledger;
        const InventoryOwner player{ InventoryOwnerKind::Player, 1 };
        const InventoryOwner container{ InventoryOwnerKind::Container, 8, "Balmora" };
        EXPECT(ledger.apply(container, InventoryAction::Set,
                   { item("diamond", 2), item("gold_001", 50) }).applied());
        EXPECT(ledger.apply(player, InventoryAction::Set, {}).applied());
        EXPECT(ledger.transfer(container, player, { item("diamond", 1) }).applied());
        EXPECT(ledger.snapshot(container)->at(0).count == 1);
        EXPECT(ledger.snapshot(player)->at(0).count == 1);

        EXPECT(ledger.transfer(container, player, { item("diamond", 2) }).decision
            == InventoryDecision::InsufficientItems);
        EXPECT(ledger.snapshot(container)->at(0).count == 1);
        EXPECT(ledger.snapshot(player)->at(0).count == 1);
    }

    void testLimits()
    {
        InventoryLedger ledger(1);
        const InventoryOwner first{ InventoryOwnerKind::Player, 1 };
        const InventoryOwner second{ InventoryOwnerKind::Player, 2 };
        EXPECT(ledger.apply({}, InventoryAction::Set, {}).decision
            == InventoryDecision::InvalidOwner);
        EXPECT(ledger.apply(first, InventoryAction::Set, {}).applied());
        EXPECT(ledger.apply(second, InventoryAction::Set, {}).decision
            == InventoryDecision::OwnerLimitReached);
        EXPECT(ledger.apply(first, InventoryAction::Add,
                   { item("overflow", InventoryLedger::MaximumStackCount) }).applied());
        EXPECT(ledger.apply(first, InventoryAction::Add, { item("overflow", 1) }).decision
            == InventoryDecision::CountOverflow);
        EXPECT(std::string(describe(InventoryDecision::InsufficientItems))
            == "the inventory does not contain the requested count");
    }

    void testContainerOwnersAreCellScoped()
    {
        InventoryLedger ledger;
        const InventoryOwner balmora{ InventoryOwnerKind::Container, 8, "Balmora" };
        const InventoryOwner aldRuhn{ InventoryOwnerKind::Container, 8, "Ald-ruhn" };

        EXPECT(ledger.apply(balmora, InventoryAction::Set,
                   { item("gold_001", 10) }).applied());
        EXPECT(ledger.apply(aldRuhn, InventoryAction::Set,
                   { item("gold_001", 25) }).applied());
        EXPECT(ledger.snapshot(balmora)->front().count == 10);
        EXPECT(ledger.snapshot(aldRuhn)->front().count == 25);
        EXPECT(ledger.size() == 2);

        EXPECT(ledger.apply({ InventoryOwnerKind::Container, 8 },
                   InventoryAction::Set, {}).decision == InventoryDecision::InvalidOwner);
        EXPECT(ledger.apply({ InventoryOwnerKind::Player, 7, "Balmora" },
                   InventoryAction::Set, {}).decision == InventoryDecision::InvalidOwner);
    }

    void testBatchIsAtomic()
    {
        InventoryLedger ledger;
        const InventoryOwner first{ InventoryOwnerKind::Container, 8, "Balmora" };
        const InventoryOwner second{ InventoryOwnerKind::Container, 9, "Balmora" };
        EXPECT(ledger.apply(first, InventoryAction::Set,
                   { item("gold_001", 10) }).applied());
        EXPECT(ledger.apply(second, InventoryAction::Set,
                   { item("diamond", 1) }).applied());

        const std::vector<InventoryOperation> invalid{
            { first, InventoryAction::Remove, { item("gold_001", 5) } },
            { second, InventoryAction::Remove, { item("diamond", 2) } },
        };
        EXPECT(ledger.previewBatch(invalid).decision
            == InventoryDecision::InsufficientItems);
        EXPECT(ledger.applyBatch(invalid).decision
            == InventoryDecision::InsufficientItems);
        EXPECT(ledger.snapshot(first)->front().count == 10);
        EXPECT(ledger.snapshot(second)->front().count == 1);

        const std::vector<InventoryOperation> valid{
            { first, InventoryAction::Remove, { item("gold_001", 5) } },
            { second, InventoryAction::Add, { item("gold_001", 5) } },
        };
        EXPECT(ledger.previewBatch(valid).applied());
        EXPECT(ledger.snapshot(first)->front().count == 10);
        EXPECT(ledger.applyBatch(valid).applied());
        EXPECT(ledger.snapshot(first)->front().count == 5);
        EXPECT(ledger.snapshot(second)->size() == 2);
    }

    void testSwap()
    {
        InventoryLedger first;
        InventoryLedger second;
        const InventoryOwner owner{ InventoryOwnerKind::Player, 1 };
        EXPECT(first.apply(owner, InventoryAction::Set,
                   { item("gold_001", 10) }).applied());
        second.swap(first);
        EXPECT(!first.snapshot(owner).has_value());
        EXPECT(second.snapshot(owner)->front().count == 10);
    }
}

int runInventoryTests()
{
    testSetAddRemove();
    testTransactionalFailure();
    testPreviewDoesNotMutate();
    testAtomicTransfer();
    testLimits();
    testContainerOwnersAreCellScoped();
    testBatchIsAtomic();
    testSwap();
    return sFailures;
}
