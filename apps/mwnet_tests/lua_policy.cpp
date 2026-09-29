#include <components/openmw-mp/Script/LuaApiPolicy.hpp>

#include <iostream>

namespace
{
    int sFailures = 0;

    void expect(bool condition, const char* expression, int line)
    {
        if (condition)
            return;
        std::cerr << "lua_policy.cpp:" << line << ": expectation failed: "
                  << expression << '\n';
        ++sFailures;
    }

#define EXPECT(condition) expect((condition), #condition, __LINE__)

    void testIntentCallbackClassification()
    {
        EXPECT(mwmp::script::isIntentCallback("OnPlayerAttackIntent"));
        EXPECT(mwmp::script::isIntentCallback("OnContainerIntent"));
        EXPECT(!mwmp::script::isIntentCallback("OnPlayerAttackIntentRejected"));
        EXPECT(!mwmp::script::isIntentCallback("OnPlayerAttack"));
    }

    void testReadOnlyApiClassification()
    {
        EXPECT(mwmp::script::isReadOnlyApi("GetHealthCurrent"));
        EXPECT(mwmp::script::isReadOnlyApi("IsPlayerLoggedIn"));
        EXPECT(mwmp::script::isReadOnlyApi("HasItemEquipped"));
        EXPECT(mwmp::script::isReadOnlyApi("DoesPlayerHavePlayerKiller"));
        EXPECT(mwmp::script::isReadOnlyApi("LogMessage"));
        EXPECT(mwmp::script::isReadOnlyApi("ReadReceivedActorList"));
        EXPECT(mwmp::script::isReadOnlyApi("Kick"));
        EXPECT(mwmp::script::isReadOnlyApi("SendMessage"));
        EXPECT(!mwmp::script::isReadOnlyApi("SetHealthCurrent"));
        EXPECT(!mwmp::script::isReadOnlyApi("AddItemChange"));
        EXPECT(!mwmp::script::isReadOnlyApi("SendStatsDynamic"));
        EXPECT(!mwmp::script::isReadOnlyApi("ClearInventoryChanges"));
        EXPECT(mwmp::script::isIntentModifierApi("SetPlayerAttackStrength"));
        EXPECT(mwmp::script::isIntentModifierApi("SetActorAttackStrength"));
        EXPECT(mwmp::script::isIntentModifierApi("AddItemChange"));
        EXPECT(mwmp::script::isIntentModifierApi("SetBounty"));
        EXPECT(mwmp::script::intentModifierUsesPlayerId("SetBounty"));
        EXPECT(!mwmp::script::intentModifierUsesPlayerId(
            "SetActorAttackStrength"));
        EXPECT(!mwmp::script::isIntentModifierApi("SetHealthCurrent"));
    }
}

int runLuaPolicyTests()
{
    testIntentCallbackClassification();
    testReadOnlyApiClassification();
    return sFailures;
}
