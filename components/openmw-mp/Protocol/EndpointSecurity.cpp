#include "EndpointSecurity.hpp"

#include <charconv>
#include <cstdint>

namespace mwmp::protocol
{
    bool isLoopbackAddress(std::string_view address) noexcept
    {
        if (address == "::1")
            return true;

        std::uint32_t octets[4]{};
        for (std::size_t index = 0; index < 4; ++index)
        {
            const std::size_t separator = address.find('.');
            const std::string_view octet = address.substr(0, separator);
            if (octet.empty())
                return false;

            const char* const first = octet.data();
            const char* const last = first + octet.size();
            const auto [end, error] = std::from_chars(first, last, octets[index]);
            if (error != std::errc{} || end != last || octets[index] > 255)
                return false;

            if (index == 3)
                return separator == std::string_view::npos && octets[0] == 127;
            if (separator == std::string_view::npos)
                return false;
            address.remove_prefix(separator + 1);
        }

        return false;
    }

    bool isListenAddressAllowed(std::string_view address, bool publicListen) noexcept
    {
        return publicListen || isLoopbackAddress(address);
    }
}
