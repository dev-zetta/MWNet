#include <components/openmw-mp/Mechanics/ObjectStateLedger.hpp>

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
        std::cerr << "object_state.cpp:" << line << ": expectation failed: "
                  << expression << '\n';
        ++sFailures;
    }

#define EXPECT(condition) expect((condition), #condition, __LINE__)

    ObjectState object(std::uint32_t refNum, std::uint32_t mpNum,
        std::uint64_t creator = 0)
    {
        ObjectState result;
        result.identity = { "Balmora", refNum, mpNum };
        result.refId = "misc_com_bottle_01";
        result.creator = creator;
        return result;
    }

    void testPlaceAndMutationLifecycle()
    {
        ObjectStateLedger ledger;
        const ObjectState placed = object(0, 42, 7);
        EXPECT(ledger.previewBatch({ { ObjectMutationKind::Place, placed } }).applied());
        EXPECT(ledger.size() == 0);
        EXPECT(ledger.applyBatch({ { ObjectMutationKind::Place, placed } }).applied());
        EXPECT(ledger.dynamicObjectCount(7) == 1);

        ObjectState moved = placed;
        moved.position = { 1.0, 2.0, 3.0 };
        ObjectState scaled = placed;
        scaled.scale = 1.5;
        EXPECT(ledger.applyBatch({
            { ObjectMutationKind::Move, moved },
            { ObjectMutationKind::Scale, scaled },
        }).applied());
        EXPECT(ledger.find(placed.identity)->position == moved.position);
        EXPECT(ledger.find(placed.identity)->scale == 1.5);

        EXPECT(ledger.applyBatch(
            { { ObjectMutationKind::Delete, placed } }).applied());
        EXPECT(ledger.find(placed.identity)->deleted);
        EXPECT(ledger.applyBatch(
            { { ObjectMutationKind::Move, moved } }).decision
            == ObjectDecision::DeletedObject);
    }

    void testAtomicFailure()
    {
        ObjectStateLedger ledger;
        ObjectState first = object(1, 0);
        ObjectState second = object(2, 0);
        EXPECT(ledger.applyBatch({
            { ObjectMutationKind::Seed, first },
            { ObjectMutationKind::Seed, second },
        }).applied());

        ObjectState moved = first;
        moved.position = { 5.0, 6.0, 7.0 };
        ObjectState invalid = second;
        invalid.scale = std::numeric_limits<double>::quiet_NaN();
        EXPECT(ledger.applyBatch({
            { ObjectMutationKind::Move, moved },
            { ObjectMutationKind::Scale, invalid },
        }).decision == ObjectDecision::InvalidObject);
        EXPECT(ledger.find(first.identity)->position == first.position);
        EXPECT(ledger.find(second.identity)->scale == second.scale);
    }

    void testQuotasAndBounds()
    {
        ObjectStateLedger ledger(2, 1);
        EXPECT(ledger.applyBatch({
            { ObjectMutationKind::Place, object(0, 1, 7) } }).applied());
        EXPECT(ledger.applyBatch({
            { ObjectMutationKind::Place, object(0, 2, 7) } }).decision
            == ObjectDecision::PlayerLimitReached);
        EXPECT(ledger.applyBatch({
            { ObjectMutationKind::Place, object(0, 2, 8) } }).applied());
        EXPECT(ledger.applyBatch({
            { ObjectMutationKind::Seed, object(3, 0) } }).decision
            == ObjectDecision::ObjectLimitReached);

        ObjectState invalid = object(4, 0);
        invalid.position[0] = ObjectStateLedger::MaximumCoordinate + 1.0;
        EXPECT(ledger.previewBatch({
            { ObjectMutationKind::Seed, invalid } }).decision
            == ObjectDecision::InvalidObject);
        invalid = object(0, 0);
        EXPECT(ledger.previewBatch({
            { ObjectMutationKind::Seed, invalid } }).decision
            == ObjectDecision::InvalidIdentity);
        EXPECT(std::string(describe(ObjectDecision::PlayerLimitReached))
            == "the per-player object quota was reached");

        ObjectStateLedger staticLedger(4, 1);
        EXPECT(staticLedger.applyBatch({
            { ObjectMutationKind::Seed, object(10, 0, 9) } }).applied());
        EXPECT(staticLedger.applyBatch({
            { ObjectMutationKind::Seed, object(11, 0, 9) } }).decision
            == ObjectDecision::PlayerLimitReached);
    }

    void testBatchLimit()
    {
        ObjectStateLedger ledger;
        std::vector<ObjectMutation> mutations(
            ObjectStateLedger::MaximumMutations + 1);
        EXPECT(ledger.previewBatch(mutations).decision
            == ObjectDecision::MutationLimitReached);
        EXPECT(ObjectStateLedger::MaximumChanges == 3000);
    }
}

int runObjectStateTests()
{
    testPlaceAndMutationLifecycle();
    testAtomicFailure();
    testQuotasAndBounds();
    testBatchLimit();
    return sFailures;
}
