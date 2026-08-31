#include <components/openmw-mp/Mechanics/PlayerProgressionLedger.hpp>

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
        std::cerr << "progression.cpp:" << line << ": expectation failed: "
                  << expression << '\n';
        ++sFailures;
    }

#define EXPECT(condition) expect((condition), #condition, __LINE__)

    PlayerProgressionState initialState()
    {
        PlayerProgressionState state;
        for (ProgressionStat& stat : state.attributes)
            stat = { 40, 40, 40, 0, 0 };
        for (ProgressionStat& stat : state.skills)
            stat = { 10, 10, 10, 0, 0 };
        return state;
    }

    void testAttributeIntentIsTransactional()
    {
        PlayerProgressionLedger ledger;
        PlayerProgressionState state = initialState();
        state.levelProgress = 10;
        EXPECT(ledger.set(7, state).applied());

        AttributeProgressionChange allowed{ 0, state.attributes[0], 0 };
        allowed.stat.base += 5;
        EXPECT(ledger.previewAttributes(7, false, { &allowed, 1 }).applied());
        EXPECT(ledger.find(7)->attributes[0].base == 40);
        EXPECT(ledger.applyAttributes(7, false, { &allowed, 1 }).applied());
        EXPECT(ledger.find(7)->attributes[0].base == 45);

        AttributeProgressionChange duplicate[] = { allowed, allowed };
        EXPECT(ledger.applyAttributes(7, false, duplicate).decision
            == ProgressionDecision::DuplicateIndex);
        EXPECT(ledger.find(7)->attributes[0].base == 45);
        EXPECT(ledger.applyAttributes(7, true, { &allowed, 1 }).decision
            == ProgressionDecision::FullClientSnapshot);
    }

    void testSkillAndLevelBounds()
    {
        PlayerProgressionLedger ledger;
        PlayerProgressionState state = initialState();
        EXPECT(ledger.set(3, state).applied());

        SkillProgressionChange skill{ 5, state.skills[5] };
        skill.stat.base += 1;
        EXPECT(ledger.applySkills(3, false, { &skill, 1 }).applied());
        skill.stat.base += 2;
        EXPECT(ledger.applySkills(3, false, { &skill, 1 }).decision
            == ProgressionDecision::UnauthorizedSkillChange);

        EXPECT(ledger.applyLevel(3, 2, 0).decision
            == ProgressionDecision::UnauthorizedLevelChange);
        EXPECT(ledger.applyLevel(3, 1, 10).applied());
        EXPECT(ledger.applyLevel(3, 2, 0).applied());
        EXPECT(ledger.find(3)->level == 2);
    }

    void testInvalidValuesAndCapacity()
    {
        PlayerProgressionLedger ledger(1);
        PlayerProgressionState state = initialState();
        state.skills[0].progress = std::numeric_limits<float>::quiet_NaN();
        EXPECT(ledger.set(1, state).decision == ProgressionDecision::InvalidStat);
        state = initialState();
        EXPECT(ledger.set(1, state).applied());
        EXPECT(ledger.set(2, state).decision == ProgressionDecision::CapacityReached);

        SkillProgressionChange invalid{ 27, {} };
        EXPECT(ledger.previewSkills(1, false, { &invalid, 1 }).decision
            == ProgressionDecision::InvalidIndex);
        EXPECT(ledger.erase(1));
        EXPECT(ledger.size() == 0);
        EXPECT(std::string(describe(ProgressionDecision::FullClientSnapshot))
            == "clients may not replace the full progression state");
    }
}

int runProgressionTests()
{
    testAttributeIntentIsTransactional();
    testSkillAndLevelBounds();
    testInvalidValuesAndCapacity();
    return sFailures;
}
