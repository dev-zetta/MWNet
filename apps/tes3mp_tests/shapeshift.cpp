#include <components/openmw-mp/Mechanics/ShapeshiftLedger.hpp>

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
        std::cerr << "shapeshift.cpp:" << line << ": expectation failed: "
                  << expression << '\n';
        ++sFailures;
    }

#define EXPECT(condition) expect((condition), #condition, __LINE__)

    void testServerStateAndClientIntent()
    {
        ShapeshiftLedger ledger;
        ShapeshiftState initial{ 1.25, false, true, "rat" };
        EXPECT(ledger.set(7, initial).applied());

        ShapeshiftState intent = initial;
        intent.isWerewolf = true;
        EXPECT(ledger.previewClientIntent(7, intent).applied());
        EXPECT(!ledger.find(7)->isWerewolf);
        EXPECT(ledger.applyClientIntent(7, intent).applied());
        EXPECT(ledger.find(7)->isWerewolf);

        intent.scale = 2.0;
        EXPECT(ledger.applyClientIntent(7, intent).decision
            == ShapeshiftDecision::UnauthorizedAppearanceChange);
        EXPECT(ledger.find(7)->scale == 1.25);
    }

    void testBoundsAndCapacity()
    {
        ShapeshiftLedger ledger(1);
        EXPECT(ledger.set(0, {}).decision == ShapeshiftDecision::InvalidPlayer);
        EXPECT(ledger.previewClientIntent(9, {}).decision
            == ShapeshiftDecision::UnknownPlayer);

        ShapeshiftState invalid;
        invalid.scale = std::numeric_limits<double>::quiet_NaN();
        EXPECT(ledger.set(1, invalid).decision == ShapeshiftDecision::InvalidScale);
        invalid.scale = 1.0;
        invalid.creatureRefId = std::string(
            ShapeshiftLedger::MaximumCreatureRefIdBytes + 1, 'x');
        EXPECT(ledger.set(1, invalid).decision
            == ShapeshiftDecision::InvalidCreatureRefId);

        EXPECT(ledger.set(1, {}).applied());
        EXPECT(ledger.set(2, {}).decision == ShapeshiftDecision::CapacityReached);
        EXPECT(ledger.erase(1));
        EXPECT(ledger.size() == 0);
        EXPECT(std::string(describe(
            ShapeshiftDecision::UnauthorizedAppearanceChange))
            == "only the server may change player scale or disguise");
    }
}

int runShapeshiftTests()
{
    testServerStateAndClientIntent();
    testBoundsAndCapacity();
    return sFailures;
}
