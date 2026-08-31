#include <components/openmw-mp/Mechanics/SpellbookLedger.hpp>

#include <iostream>
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
        std::cerr << "spellbook.cpp:" << line << ": expectation failed: "
                  << expression << '\n';
        ++sFailures;
    }

#define EXPECT(condition) expect((condition), #condition, __LINE__)

    void testSetAddRemove()
    {
        SpellbookLedger ledger;
        EXPECT(ledger.apply(7, SpellbookAction::Set,
                   { "fireball", "hearth_heal" }).applied());
        EXPECT(ledger.contains(7, "fireball"));
        EXPECT(ledger.apply(7, SpellbookAction::Add, { "water_walking" }).applied());
        EXPECT(ledger.snapshot(7)->size() == 3);
        EXPECT(ledger.apply(7, SpellbookAction::Remove, { "fireball" }).applied());
        EXPECT(!ledger.contains(7, "fireball"));
    }

    void testFailuresAreTransactional()
    {
        SpellbookLedger ledger;
        EXPECT(ledger.apply(1, SpellbookAction::Set, { "fireball" }).applied());
        EXPECT(ledger.apply(1, SpellbookAction::Remove,
                   { "fireball", "missing" }).decision
            == SpellbookDecision::MissingSpell);
        EXPECT(ledger.contains(1, "fireball"));
        EXPECT(ledger.apply(1, SpellbookAction::Add,
                   { "duplicate", "duplicate" }).decision
            == SpellbookDecision::InvalidSpell);
        EXPECT(!ledger.contains(1, "duplicate"));
    }

    void testPreviewAndLimits()
    {
        SpellbookLedger ledger(1);
        EXPECT(ledger.apply(0, SpellbookAction::Set, {}).decision
            == SpellbookDecision::InvalidOwner);
        EXPECT(ledger.apply(1, SpellbookAction::Set, { "fireball" }).applied());
        EXPECT(ledger.preview(1, SpellbookAction::Add, { "heal" }).applied());
        EXPECT(!ledger.contains(1, "heal"));
        EXPECT(ledger.apply(2, SpellbookAction::Set, {}).decision
            == SpellbookDecision::OwnerLimitReached);
        EXPECT(ledger.apply(1, SpellbookAction::Add, { "bad\nspell" }).decision
            == SpellbookDecision::InvalidSpell);
        EXPECT(std::string(describe(SpellbookDecision::MissingSpell))
            == "the spellbook does not contain the spell");
    }
}

int runSpellbookTests()
{
    testSetAddRemove();
    testFailuresAreTransactional();
    testPreviewAndLimits();
    return sFailures;
}
