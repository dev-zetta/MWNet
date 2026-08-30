#ifndef OPENMW_MP_PROTOCOL_ENDPOINT_SECURITY_HPP
#define OPENMW_MP_PROTOCOL_ENDPOINT_SECURITY_HPP

#include <string_view>

namespace mwmp::protocol
{
    bool isLoopbackAddress(std::string_view address) noexcept;
    bool isListenAddressAllowed(std::string_view address, bool publicListen) noexcept;
}

#endif
