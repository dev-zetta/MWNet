#include <components/openmw-mp/Version.hpp>

#include <iostream>
#include <string_view>

int runProtocolTests();
int runTransportTests();

int main()
{
    static_assert(TES3MP_PROTO_VERSION == 11, "TES3MP hardening requires protocol 11");

    if (std::string_view(TES3MP_VERSION) != "1.0.0-alpha.1")
    {
        std::cerr << "Unexpected TES3MP version: " << TES3MP_VERSION << '\n';
        return 1;
    }

    const int failures = runProtocolTests() + runTransportTests();
    return failures == 0 ? 0 : 1;
}
