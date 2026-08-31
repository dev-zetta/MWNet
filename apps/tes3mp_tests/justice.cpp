#include <components/openmw-mp/Mechanics/JusticeLedger.hpp>

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
        std::cerr << "justice.cpp:" << line << ": expectation failed: "
                  << expression << '\n';
        ++sFailures;
    }

#define EXPECT(condition) expect((condition), #condition, __LINE__)

    void testServerBountyAndClientIntent()
    {
        JusticeLedger ledger;
        EXPECT(ledger.setBounty(7, 100).applied());
        EXPECT(ledger.previewBountyIntent(7, 150).applied());
        EXPECT(ledger.find(7)->bounty == 100);
        EXPECT(ledger.applyBountyIntent(7, 150).applied());
        EXPECT(ledger.find(7)->bounty == 150);
        EXPECT(ledger.applyBountyIntent(7, 0).decision
            == JusticeDecision::UnauthorizedBountyReduction);
        EXPECT(ledger.find(7)->bounty == 150);
        EXPECT(ledger.setBounty(7, 0).applied());
        EXPECT(ledger.find(7)->bounty == 0);
    }

    void testSentenceLifecycle()
    {
        JusticeLedger ledger;
        EXPECT(ledger.setBounty(9, 108).applied());
        const JusticeResult begun = ledger.beginSentence(
            9, 5, false, false, "Serving sentence", "Released");
        EXPECT(begun.applied());
        EXPECT(begun.state.sentence.has_value());
        const std::uint64_t sentenceId = begun.state.sentence->id;
        EXPECT(ledger.beginSentence(9, 1, false, false, {}, {}).decision
            == JusticeDecision::SentenceAlreadyActive);
        EXPECT(ledger.previewSentenceCompletion(9, sentenceId).applied());
        EXPECT(ledger.find(9)->sentence.has_value());
        EXPECT(ledger.completeSentence(9, sentenceId + 1, true).decision
            == JusticeDecision::StaleSentence);
        EXPECT(ledger.completeSentence(9, sentenceId, true).applied());
        EXPECT(!ledger.find(9)->sentence.has_value());
        EXPECT(ledger.find(9)->bounty == 0);
    }

    void testBoundsAndCapacity()
    {
        JusticeLedger ledger(1);
        EXPECT(ledger.setBounty(0, 1).decision == JusticeDecision::InvalidPlayer);
        EXPECT(ledger.setBounty(1, -1).decision == JusticeDecision::InvalidBounty);
        EXPECT(ledger.setBounty(1, JusticeLedger::MaximumBounty + 1).decision
            == JusticeDecision::InvalidBounty);
        EXPECT(ledger.setBounty(1, 0).applied());
        EXPECT(ledger.setBounty(2, 0).decision == JusticeDecision::CapacityReached);
        EXPECT(ledger.beginSentence(1, 0, false, false, {}, {}).decision
            == JusticeDecision::InvalidSentence);
        EXPECT(ledger.beginSentence(1, 1, false, false,
            std::string(JusticeLedger::MaximumTextBytes + 1, 'x'), {}).decision
            == JusticeDecision::InvalidSentence);
        EXPECT(std::string(describe(JusticeDecision::UnauthorizedBountyReduction))
            == "only the server may reduce a bounty");
        EXPECT(ledger.erase(1));
        EXPECT(ledger.size() == 0);
    }
}

int runJusticeTests()
{
    testServerBountyAndClientIntent();
    testSentenceLifecycle();
    testBoundsAndCapacity();
    return sFailures;
}
