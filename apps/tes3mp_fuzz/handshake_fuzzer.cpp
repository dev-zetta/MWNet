#include <components/openmw-mp/Security/SecureSession.hpp>

#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    const auto bytes = std::as_bytes(std::span(data, size));
    mwmp::security::ClientHello client;
    mwmp::security::ServerHello server;
    (void)mwmp::security::decodeClientHello(bytes, client);
    (void)mwmp::security::decodeServerHello(bytes, server);
    return 0;
}
