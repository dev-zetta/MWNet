#include <components/openmw-mp/Version.hpp>

#include <iostream>
#include <string_view>

int runProtocolTests();
int runActiveEffectTests();
int runActorStateTests();
int runAuthorityTests();
int runCastTests();
int runCombatTests();
int runEquipmentTests();
int runInventoryTests();
int runJusticeTests();
int runMovementTests();
int runObjectStateTests();
int runOwnershipTests();
int runLifecycleTests();
int runPersistenceTests();
int runSessionTests();
int runShapeshiftTests();
int runTransportTests();
#if defined(TES3MP_HAS_GNS_TRANSPORT)
int runAuthenticationTests();
int runGameNetworkingSocketsTests();
int runSecurityTests();
#endif

int main()
{
    static_assert(TES3MP_PROTO_VERSION == 11, "TES3MP hardening requires protocol 11");

    if (std::string_view(TES3MP_VERSION) != "1.0.0-alpha.1")
    {
        std::cerr << "Unexpected TES3MP version: " << TES3MP_VERSION << '\n';
        return 1;
    }

    int failures = runProtocolTests() + runActiveEffectTests() + runActorStateTests()
        + runAuthorityTests() + runCastTests() + runCombatTests()
        + runEquipmentTests() + runInventoryTests() + runJusticeTests() + runMovementTests()
        + runObjectStateTests() + runOwnershipTests()
        + runLifecycleTests() + runPersistenceTests()
        + runSessionTests() + runShapeshiftTests() + runTransportTests();
#if defined(TES3MP_HAS_GNS_TRANSPORT)
    failures += runAuthenticationTests();
    failures += runGameNetworkingSocketsTests();
    failures += runSecurityTests();
#endif
    return failures == 0 ? 0 : 1;
}
