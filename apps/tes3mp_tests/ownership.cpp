#include <components/openmw-mp/Base/BasePacketProcessor.hpp>

#include <iostream>
#include <memory>
#include <stdexcept>

namespace
{
    int failures = 0;

    void expect(bool condition, const char* expression, int line)
    {
        if (condition)
            return;
        ++failures;
        std::cerr << "ownership.cpp:" << line << ": expectation failed: " << expression << '\n';
    }

#define EXPECT(condition) expect((condition), #condition, __LINE__)

    class TestProcessor : public BasePacketProcessor<TestProcessor>
    {
    public:
        explicit TestProcessor(unsigned char id)
        {
            BPP_INIT(id)
        }

        ~TestProcessor()
        {
            ++destructionCount;
        }

        static int destructionCount;
    };

    int TestProcessor::destructionCount = 0;

    void testDuplicateProcessorOwnership()
    {
        TestProcessor::AddProcessor(std::make_unique<TestProcessor>(42));
        EXPECT(TestProcessor::destructionCount == 0);

        bool duplicateRejected = false;
        try
        {
            TestProcessor::AddProcessor(std::make_unique<TestProcessor>(42));
        }
        catch (const std::logic_error&)
        {
            duplicateRejected = true;
        }

        EXPECT(duplicateRejected);
        EXPECT(TestProcessor::destructionCount == 1);

        bool nullRejected = false;
        try
        {
            TestProcessor::AddProcessor(std::unique_ptr<TestProcessor>{});
        }
        catch (const std::invalid_argument&)
        {
            nullRejected = true;
        }
        EXPECT(nullRejected);
    }
}

template<>
BasePacketProcessor<TestProcessor>::processors_t BasePacketProcessor<TestProcessor>::processors{};

int runOwnershipTests()
{
    testDuplicateProcessorOwnership();
    return failures;
}
