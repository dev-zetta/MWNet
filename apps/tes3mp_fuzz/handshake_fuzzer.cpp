#include <components/openmw-mp/Security/SecureSession.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace
{
    template <std::size_t Size>
    void fill(std::array<unsigned char, Size>& output,
        const std::uint8_t* data, std::size_t size, std::size_t offset)
    {
        if (size == 0)
            return;
        for (std::size_t index = 0; index < Size; ++index)
            output[index] = data[(index + offset) % size];
    }

    void mutate(std::vector<std::byte>& encoded, const std::uint8_t* data,
        std::size_t size)
    {
        if (size < 2 || encoded.empty())
            return;
        const auto offset = static_cast<std::size_t>(data[0]) % encoded.size();
        encoded[offset] ^= static_cast<std::byte>(data[1]);
    }
}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    const auto bytes = std::as_bytes(std::span(data, size));
    mwmp::security::ClientHello client;
    mwmp::security::ServerHello server;
    (void)mwmp::security::decodeClientHello(bytes, client);
    (void)mwmp::security::decodeServerHello(bytes, server);

    mwmp::security::ClientHello generatedClient;
    fill(generatedClient.ephemeralPublicKey, data, size, 0);
    fill(generatedClient.nonce, data, size, 1);
    std::vector<std::byte> encoded;
    if (mwmp::security::encodeClientHello(generatedClient, encoded))
    {
        mwmp::security::ClientHello validClient;
        (void)mwmp::security::decodeClientHello(encoded, validClient);
        mutate(encoded, data, size);
        (void)mwmp::security::decodeClientHello(encoded, client);
    }

    mwmp::security::ServerHello generatedServer;
    fill(generatedServer.identityPublicKey, data, size, 0);
    fill(generatedServer.ephemeralPublicKey, data, size, 1);
    fill(generatedServer.nonce, data, size, 2);
    fill(generatedServer.signature, data, size, 3);
    if (mwmp::security::encodeServerHello(generatedServer, encoded))
    {
        mwmp::security::ServerHello validServer;
        (void)mwmp::security::decodeServerHello(encoded, validServer);
        mutate(encoded, data, size);
        (void)mwmp::security::decodeServerHello(encoded, server);
    }
    return 0;
}
