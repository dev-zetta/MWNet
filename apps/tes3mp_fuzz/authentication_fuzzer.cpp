#include <components/openmw-mp/Security/AuthenticationMessages.hpp>

#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    const auto bytes = std::as_bytes(std::span(data, size));
    mwmp::security::AuthenticationRequest request;
    mwmp::security::AuthenticationResponse response;
    (void)mwmp::security::decodeAuthenticationRequest(bytes, request);
    (void)mwmp::security::decodeAuthenticationResponse(bytes, response);
    return 0;
}
