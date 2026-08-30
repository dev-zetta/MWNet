#ifndef OPENMW_MP_SECURITY_SECURE_SESSION_HPP
#define OPENMW_MP_SECURITY_SECURE_SESSION_HPP

#include "ServerIdentity.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <sodium.h>

namespace mwmp::security
{
    enum class SecurityError
    {
        None,
        InvalidMessage,
        InvalidSignature,
        KeyExchangeFailed,
        AuthenticationFailed,
        ReplayDetected,
        LimitExceeded,
        CounterExhausted,
        AllocationFailed,
    };

    struct ClientHello
    {
        std::array<unsigned char, crypto_kx_PUBLICKEYBYTES> ephemeralPublicKey{};
        std::array<unsigned char, crypto_aead_xchacha20poly1305_ietf_NPUBBYTES> nonce{};
    };

    struct ServerHello
    {
        std::array<unsigned char, crypto_sign_PUBLICKEYBYTES> identityPublicKey{};
        std::array<unsigned char, crypto_kx_PUBLICKEYBYTES> ephemeralPublicKey{};
        std::array<unsigned char, crypto_aead_xchacha20poly1305_ietf_NPUBBYTES> nonce{};
        std::array<unsigned char, crypto_sign_BYTES> signature{};
    };

    class SecureSession
    {
    public:
        SecureSession() = default;
        SecureSession(SecureSession&& other) noexcept;
        SecureSession& operator=(SecureSession&& other) noexcept;
        ~SecureSession();

        SecureSession(const SecureSession&) = delete;
        SecureSession& operator=(const SecureSession&) = delete;

        explicit operator bool() const noexcept { return mValid; }
        bool seal(std::span<const std::byte> plaintext, std::vector<std::byte>& ciphertext,
            SecurityError& error) noexcept;
        bool open(std::span<const std::byte> ciphertext, std::vector<std::byte>& plaintext,
            SecurityError& error) noexcept;

    private:
        friend class ClientHandshake;
        friend class ServerHandshake;
        SecureSession(std::array<unsigned char, crypto_kx_SESSIONKEYBYTES> receiveKey,
            std::array<unsigned char, crypto_kx_SESSIONKEYBYTES> sendKey) noexcept;
        void clear() noexcept;

        std::array<unsigned char, crypto_kx_SESSIONKEYBYTES> mReceiveKey{};
        std::array<unsigned char, crypto_kx_SESSIONKEYBYTES> mSendKey{};
        std::uint64_t mReceiveCounter = 0;
        std::uint64_t mSendCounter = 0;
        bool mValid = false;
    };

    class ClientHandshake
    {
    public:
        ClientHandshake();
        ~ClientHandshake();

        ClientHandshake(const ClientHandshake&) = delete;
        ClientHandshake& operator=(const ClientHandshake&) = delete;

        explicit operator bool() const noexcept { return mInitialized; }
        const ClientHello& hello() const noexcept { return mHello; }
        bool finish(const ServerHello& response, SecureSession& session, SecurityError& error) noexcept;

    private:
        ClientHello mHello;
        std::array<unsigned char, crypto_kx_SECRETKEYBYTES> mSecretKey{};
        bool mInitialized = false;
        bool mFinished = false;
    };

    class ServerHandshake
    {
    public:
        explicit ServerHandshake(const ServerIdentity& identity) noexcept;
        bool accept(const ClientHello& hello, ServerHello& response, SecureSession& session,
            SecurityError& error) const noexcept;

    private:
        const ServerIdentity& mIdentity;
    };

    bool encodeClientHello(const ClientHello& hello, std::vector<std::byte>& output) noexcept;
    bool decodeClientHello(std::span<const std::byte> input, ClientHello& hello) noexcept;
    bool encodeServerHello(const ServerHello& hello, std::vector<std::byte>& output) noexcept;
    bool decodeServerHello(std::span<const std::byte> input, ServerHello& hello) noexcept;
    std::string fingerprint(const std::array<unsigned char, crypto_sign_PUBLICKEYBYTES>& publicKey);
}

#endif
