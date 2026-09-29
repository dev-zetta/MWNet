#include <components/openmw-mp/Version.hpp>

int runProtocolTests();
int runProgressionTests();
int runActiveEffectTests();
int runActorMagicTests();
int runActorStateTests();
int runAuthorityTests();
int runCastTests();
int runCombatTests();
int runEquipmentTests();
int runInventoryTests();
int runItemUseTests();
int runJusticeTests();
int runMovementTests();
int runMetricsTests();
int runNativeFunctionTests();
int runSoakMemoryTests();
int runObjectStateTests();
int runOwnershipTests();
int runLifecycleTests();
int runLuaPolicyTests();
int runPersistenceTests();
int runSessionTests();
int runShapeshiftTests();
int runSpellTests();
int runSpellbookTests();
#if defined(MWNET_HAS_TIMED_LOG_TESTS)
int runTimedLogTests();
#endif
int runTransportTests();
#if defined(MWNET_HAS_SECURITY_TESTS)
int runAuthenticationTests();
int runSecurityTests();
#endif
#if defined(MWNET_HAS_GNS_TRANSPORT)
int runGameNetworkingSocketsTests();
#endif

int main()
{
    static_assert(MWNET_PROTO_VERSION == 13, "MWNet uses protocol 13 to distinguish pre-rebrand peers");

    int failures = runProtocolTests() + runActiveEffectTests() + runActorMagicTests()
        + runActorStateTests()
        + runAuthorityTests() + runCastTests() + runCombatTests()
        + runEquipmentTests() + runInventoryTests() + runItemUseTests()
        + runJusticeTests() + runMovementTests()
        + runMetricsTests()
        + runNativeFunctionTests()
        + runSoakMemoryTests()
        + runObjectStateTests() + runOwnershipTests() + runLuaPolicyTests()
        + runLifecycleTests() + runPersistenceTests() + runProgressionTests()
        + runSessionTests() + runShapeshiftTests()
        + runSpellTests() + runSpellbookTests() + runTransportTests();
#if defined(MWNET_HAS_TIMED_LOG_TESTS)
    failures += runTimedLogTests();
#endif
#if defined(MWNET_HAS_SECURITY_TESTS)
    failures += runAuthenticationTests();
    failures += runSecurityTests();
#endif
#if defined(MWNET_HAS_GNS_TRANSPORT)
    failures += runGameNetworkingSocketsTests();
#endif
    return failures == 0 ? 0 : 1;
}
