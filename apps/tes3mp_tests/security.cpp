#include <components/openmw-mp/Security/SecureSession.hpp>
#include <components/openmw-mp/Security/ServerIdentity.hpp>
#include <components/openmw-mp/Security/TrustStore.hpp>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using namespace mwmp::security;

    int sFailures = 0;

    void expect(bool condition, const char* expression, int line)
    {
        if (condition)
            return;
        std::cerr << "security.cpp:" << line << ": expectation failed: " << expression << '\n';
        ++sFailures;
    }

#define EXPECT(condition) expect((condition), #condition, __LINE__)

    std::span<const std::byte> bytes(std::string_view value)
    {
        return std::as_bytes(std::span(value));
    }
}

int runSecurityTests()
{
    const auto unique = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const auto directory = std::filesystem::temp_directory_path() / ("tes3mp-security-" + unique);
    const auto identityPath = directory / "server-identity.key";
    const auto trustPath = directory / "trusted-servers.json";
    std::string error;

    auto identity = ServerIdentity::loadOrCreate(identityPath, error);
    EXPECT(identity.has_value());
    EXPECT(error.empty());
    EXPECT(identity && TrustStore::isValidFingerprint(identity->fingerprint()));

    auto reloadedIdentity = ServerIdentity::loadOrCreate(identityPath, error);
    EXPECT(reloadedIdentity.has_value());
    EXPECT(identity && reloadedIdentity && identity->fingerprint() == reloadedIdentity->fingerprint());

    ClientHandshake client;
    ServerHandshake server(*identity);
    ServerHello response;
    SecureSession serverSession;
    SecurityError securityError = SecurityError::None;
    EXPECT(server.accept(client.hello(), response, serverSession, securityError));
    SecureSession clientSession;
    EXPECT(client.finish(response, clientSession, securityError));
    EXPECT(static_cast<bool>(clientSession));
    EXPECT(static_cast<bool>(serverSession));
    EXPECT(fingerprint(response.identityPublicKey) == identity->fingerprint());

    std::vector<std::byte> encodedHello;
    EXPECT(encodeClientHello(client.hello(), encodedHello));
    ClientHello decodedHello;
    EXPECT(decodeClientHello(encodedHello, decodedHello));
    encodedHello.push_back(std::byte{ 0 });
    EXPECT(!decodeClientHello(encodedHello, decodedHello));

    std::vector<std::byte> encodedResponse;
    EXPECT(encodeServerHello(response, encodedResponse));
    ServerHello decodedResponse;
    EXPECT(decodeServerHello(encodedResponse, decodedResponse));

    std::vector<std::byte> encrypted;
    EXPECT(clientSession.seal(bytes("authenticated payload"), encrypted, securityError));
    std::vector<std::byte> decrypted;
    EXPECT(serverSession.open(encrypted, decrypted, securityError));
    EXPECT(decrypted == std::vector<std::byte>(bytes("authenticated payload").begin(),
                            bytes("authenticated payload").end()));
    const auto sentinel = decrypted;
    EXPECT(!serverSession.open(encrypted, decrypted, securityError));
    EXPECT(securityError == SecurityError::ReplayDetected);
    EXPECT(decrypted == sentinel);

    response.signature[0] ^= 1U;
    ClientHandshake invalidClient;
    SecureSession invalidSession;
    EXPECT(!invalidClient.finish(response, invalidSession, securityError));
    EXPECT(securityError == SecurityError::InvalidSignature);

    auto store = TrustStore::load(trustPath, error);
    EXPECT(store.has_value());
    const std::string identityFingerprint = identity->fingerprint();
    EXPECT(store->assess("LOCALHOST.", 25565, identityFingerprint)
        == TrustDecision::ConfirmationRequired);
    EXPECT(store->trust("localhost", 25565, identityFingerprint, error));
    EXPECT(store->assess("LOCALHOST", 25565, identityFingerprint) == TrustDecision::Trusted);

    const std::string otherFingerprint = fingerprint(invalidClient.hello().ephemeralPublicKey);
    EXPECT(store->assess("localhost", 25565, otherFingerprint)
        == TrustDecision::FingerprintMismatch);
    EXPECT(!store->trust("localhost", 25565, otherFingerprint, error));

    auto reloadedStore = TrustStore::load(trustPath, error);
    EXPECT(reloadedStore.has_value());
    EXPECT(reloadedStore->trustedFingerprint("localhost.", 25565)
        == std::optional(identityFingerprint));

    std::error_code cleanupError;
    std::filesystem::remove_all(directory, cleanupError);
    return sFailures;
}
