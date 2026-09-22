#include <components/openmw-mp/Version.hpp>

#include <iostream>
#include <string_view>

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
#if defined(TES3MP_HAS_TIMED_LOG_TESTS)
int runTimedLogTests();
#endif
int runTransportTests();
#if defined(TES3MP_HAS_SECURITY_TESTS)
int runAuthenticationTests();
int runSecurityTests();
#endif
#if defined(TES3MP_HAS_GNS_TRANSPORT)
int runGameNetworkingSocketsTests();
#endif

int main()
{
    static_assert(TES3MP_PROTO_VERSION == 12, "Canonical fist results require protocol 12");

    if (std::string_view(TES3MP_VERSION) != "1.0.0-alpha.1")
    {
        std::cerr << "Unexpected TES3MP version: " << TES3MP_VERSION << '\n';
        return 1;
    }

    int failures = runProtocolTests() + runActiveEffectTests() + runActorMagicTests()
        + runActorStateTests()
        + runAuthorityTests() + runCastTests() + runCombatTests()
        + runEquipmentTests() + runInventoryTests() + runItemUseTests()
        + runJusticeTests() + runMovementTests()
        + runMetricsTests()
        + runSoakMemoryTests()
        + runObjectStateTests() + runOwnershipTests() + runLuaPolicyTests()
        + runLifecycleTests() + runPersistenceTests() + runProgressionTests()
        + runSessionTests() + runShapeshiftTests()
        + runSpellTests() + runSpellbookTests() + runTransportTests();
#if defined(TES3MP_HAS_TIMED_LOG_TESTS)
    failures += runTimedLogTests();
#endif
#if defined(TES3MP_HAS_SECURITY_TESTS)
    failures += runAuthenticationTests();
    failures += runSecurityTests();
#endif
#if defined(TES3MP_HAS_GNS_TRANSPORT)
    failures += runGameNetworkingSocketsTests();
#endif
    return failures == 0 ? 0 : 1;
}
