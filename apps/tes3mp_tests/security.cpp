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
#include <utility>

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
    for (std::size_t size = 0; size < encodedHello.size(); ++size)
    {
        ClientHello truncated = decodedHello;
        EXPECT(!decodeClientHello(std::span(encodedHello).first(size), truncated));
        EXPECT(truncated.ephemeralPublicKey == decodedHello.ephemeralPublicKey);
    }
    encodedHello.push_back(std::byte{ 0 });
    EXPECT(!decodeClientHello(encodedHello, decodedHello));

    std::vector<std::byte> encodedResponse;
    EXPECT(encodeServerHello(response, encodedResponse));
    ServerHello decodedResponse;
    EXPECT(decodeServerHello(encodedResponse, decodedResponse));
    for (std::size_t size = 0; size < encodedResponse.size(); ++size)
    {
        ServerHello truncated = decodedResponse;
        EXPECT(!decodeServerHello(std::span(encodedResponse).first(size), truncated));
        EXPECT(truncated.identityPublicKey == decodedResponse.identityPublicKey);
        EXPECT(truncated.signature == decodedResponse.signature);
    }
    encodedResponse.push_back(std::byte{ 0 });
    EXPECT(!decodeServerHello(encodedResponse, decodedResponse));

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

    // Real transports can deliver independent lanes out of order and lose
    // unreliable frames. Neither event may invalidate otherwise authentic data.
    std::vector<std::byte> delayed, lost, overtaking;
    EXPECT(clientSession.seal(bytes("delayed lane"), delayed, securityError));
    EXPECT(clientSession.seal(bytes("lost snapshot"), lost, securityError));
    EXPECT(clientSession.seal(bytes("overtaking lane"), overtaking, securityError));
    EXPECT(serverSession.open(overtaking, decrypted, securityError));
    EXPECT(serverSession.open(delayed, decrypted, securityError));
    const auto beforeReplay = decrypted;
    EXPECT(!serverSession.open(delayed, decrypted, securityError));
    EXPECT(securityError == SecurityError::ReplayDetected);
    EXPECT(decrypted == beforeReplay);

    std::vector<std::byte> next;
    EXPECT(clientSession.seal(bytes("next valid frame"), next, securityError));
    auto damaged = next;
    damaged[8] ^= std::byte{ 0x40 };
    EXPECT(!serverSession.open(damaged, decrypted, securityError));
    EXPECT(securityError == SecurityError::AuthenticationFailed);
    EXPECT(decrypted == beforeReplay);
    EXPECT(serverSession.open(next, decrypted, securityError));
    EXPECT(serverSession.open(lost, decrypted, securityError));

    // Check both sides of the fixed history boundary with unseen frames.
    std::vector<std::byte> expired, boundary, newest;
    for (std::size_t index = 0; index <= SecureSession::replayWindowSize; ++index)
    {
        EXPECT(clientSession.seal(bytes("window boundary"), newest, securityError));
        if (index == 0)
            expired = newest;
        else if (index == 1)
            boundary = newest;
    }
    EXPECT(serverSession.open(newest, decrypted, securityError));
    EXPECT(serverSession.open(boundary, decrypted, securityError));
    EXPECT(!serverSession.open(expired, decrypted, securityError));
    EXPECT(securityError == SecurityError::ReplayDetected);
    SecureSession movedSession(std::move(serverSession));
    EXPECT(!static_cast<bool>(serverSession));
    EXPECT(!movedSession.open(boundary, decrypted, securityError));
    EXPECT(securityError == SecurityError::ReplayDetected);

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
