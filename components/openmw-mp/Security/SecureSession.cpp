#include "SecureSession.hpp"

#include "SodiumInit.hpp"

#include <components/openmw-mp/Protocol/PacketCodec.hpp>
#include <components/openmw-mp/Protocol/ProtocolLimits.hpp>

#include <algorithm>
#include <array>
#include <limits>
#include <new>
#include <utility>

namespace mwmp::security
{
    namespace
    {
        constexpr std::uint8_t sHandshakeVersion = 1;
        constexpr std::uint8_t sSecureFrameVersion = 1;
        constexpr std::size_t sFrameHeaderBytes = 1 + sizeof(std::uint64_t);
        constexpr std::string_view sTranscriptContext = "MWNet protocol 13 server identity";

        template <std::size_t Size>
        std::span<const std::byte> bytes(const std::array<unsigned char, Size>& value)
        {
            return std::as_bytes(std::span(value));
        }

        template <std::size_t Size>
        std::span<std::byte> writableBytes(std::array<unsigned char, Size>& value)
        {
            return std::as_writable_bytes(std::span(value));
        }

        std::vector<unsigned char> transcript(const ClientHello& client, const ServerHello& server)
        {
            std::vector<unsigned char> result;
            result.reserve(sTranscriptContext.size() + client.ephemeralPublicKey.size() + client.nonce.size()
                + server.identityPublicKey.size() + server.ephemeralPublicKey.size() + server.nonce.size());
            result.insert(result.end(), sTranscriptContext.begin(), sTranscriptContext.end());
            result.insert(result.end(), client.ephemeralPublicKey.begin(), client.ephemeralPublicKey.end());
            result.insert(result.end(), client.nonce.begin(), client.nonce.end());
            result.insert(result.end(), server.identityPublicKey.begin(), server.identityPublicKey.end());
            result.insert(result.end(), server.ephemeralPublicKey.begin(), server.ephemeralPublicKey.end());
            result.insert(result.end(), server.nonce.begin(), server.nonce.end());
            return result;
        }

        std::array<unsigned char, crypto_aead_xchacha20poly1305_ietf_NPUBBYTES> frameNonce(
            std::uint64_t counter)
        {
            std::array<unsigned char, crypto_aead_xchacha20poly1305_ietf_NPUBBYTES> nonce{};
            for (std::size_t index = 0; index < sizeof(counter); ++index)
                nonce[nonce.size() - sizeof(counter) + index]
                    = static_cast<unsigned char>((counter >> (index * 8U)) & 0xffU);
            return nonce;
        }
    }

    SecureSession::SecureSession(std::array<unsigned char, crypto_kx_SESSIONKEYBYTES> receiveKey,
        std::array<unsigned char, crypto_kx_SESSIONKEYBYTES> sendKey) noexcept
        : mReceiveKey(receiveKey)
        , mSendKey(sendKey)
        , mValid(true)
    {
    }

    SecureSession::SecureSession(SecureSession&& other) noexcept
        : mReceiveKey(other.mReceiveKey)
        , mSendKey(other.mSendKey)
        , mReceiveCounter(other.mReceiveCounter)
        , mReceiveWindow(other.mReceiveWindow)
        , mSendCounter(other.mSendCounter)
        , mValid(other.mValid)
    {
        other.clear();
    }

    SecureSession& SecureSession::operator=(SecureSession&& other) noexcept
    {
        if (this != &other)
        {
            clear();
            mReceiveKey = other.mReceiveKey;
            mSendKey = other.mSendKey;
            mReceiveCounter = other.mReceiveCounter;
            mReceiveWindow = other.mReceiveWindow;
            mSendCounter = other.mSendCounter;
            mValid = other.mValid;
            other.clear();
        }
        return *this;
    }

    SecureSession::~SecureSession()
    {
        clear();
    }

    void SecureSession::clear() noexcept
    {
        sodium_memzero(mReceiveKey.data(), mReceiveKey.size());
        sodium_memzero(mSendKey.data(), mSendKey.size());
        mReceiveCounter = 0;
        mReceiveWindow.reset();
        mSendCounter = 0;
        mValid = false;
    }

    bool SecureSession::seal(std::span<const std::byte> plaintext,
        std::vector<std::byte>& ciphertext, SecurityError& error) noexcept
    {
        error = SecurityError::None;
        if (!mValid)
        {
            error = SecurityError::KeyExchangeFailed;
            return false;
        }
        if (plaintext.size() > protocol::limits::normalMessageBytes + protocol::envelopeBytes)
        {
            error = SecurityError::LimitExceeded;
            return false;
        }
        if (mSendCounter == std::numeric_limits<std::uint64_t>::max())
        {
            error = SecurityError::CounterExhausted;
            return false;
        }

        protocol::PacketWriter header(sFrameHeaderBytes);
        header.writeU8(sSecureFrameVersion);
        header.writeU64(mSendCounter);
        try
        {
            std::vector<std::byte> encoded(
                sFrameHeaderBytes + plaintext.size() + crypto_aead_xchacha20poly1305_ietf_ABYTES);
            std::copy(header.bytes().begin(), header.bytes().end(), encoded.begin());
            unsigned long long encodedBytes = 0;
            const auto nonce = frameNonce(mSendCounter);
            if (crypto_aead_xchacha20poly1305_ietf_encrypt(
                    reinterpret_cast<unsigned char*>(encoded.data() + sFrameHeaderBytes), &encodedBytes,
                    reinterpret_cast<const unsigned char*>(plaintext.data()), plaintext.size(),
                    reinterpret_cast<const unsigned char*>(encoded.data()), sFrameHeaderBytes, nullptr,
                    nonce.data(), mSendKey.data())
                != 0)
            {
                error = SecurityError::AuthenticationFailed;
                return false;
            }
            encoded.resize(sFrameHeaderBytes + static_cast<std::size_t>(encodedBytes));
            ciphertext = std::move(encoded);
            ++mSendCounter;
            return true;
        }
        catch (const std::bad_alloc&)
        {
            error = SecurityError::AllocationFailed;
            return false;
        }
    }

    bool SecureSession::open(std::span<const std::byte> ciphertext,
        std::vector<std::byte>& plaintext, SecurityError& error) noexcept
    {
        error = SecurityError::None;
        if (!mValid || ciphertext.size() < sFrameHeaderBytes + crypto_aead_xchacha20poly1305_ietf_ABYTES)
        {
            error = SecurityError::InvalidMessage;
            return false;
        }
        if (ciphertext.size() > sFrameHeaderBytes + protocol::limits::normalMessageBytes
                + protocol::envelopeBytes + crypto_aead_xchacha20poly1305_ietf_ABYTES)
        {
            error = SecurityError::LimitExceeded;
            return false;
        }

        protocol::PacketReader header(ciphertext.first(sFrameHeaderBytes));
        std::uint8_t version = 0;
        std::uint64_t counter = 0;
        if (!header.readU8(version) || !header.readU64(counter) || !header.finish()
            || version != sSecureFrameVersion)
        {
            error = SecurityError::InvalidMessage;
            return false;
        }
        if (mReceiveWindow.test(0) && counter <= mReceiveCounter)
        {
            const auto age = mReceiveCounter - counter;
            if (age >= replayWindowSize || mReceiveWindow.test(static_cast<std::size_t>(age)))
            {
                error = SecurityError::ReplayDetected;
                return false;
            }
        }

        try
        {
            std::vector<std::byte> decoded(ciphertext.size() - sFrameHeaderBytes);
            unsigned long long decodedBytes = 0;
            const auto nonce = frameNonce(counter);
            if (crypto_aead_xchacha20poly1305_ietf_decrypt(
                    reinterpret_cast<unsigned char*>(decoded.data()), &decodedBytes, nullptr,
                    reinterpret_cast<const unsigned char*>(ciphertext.data() + sFrameHeaderBytes),
                    ciphertext.size() - sFrameHeaderBytes,
                    reinterpret_cast<const unsigned char*>(ciphertext.data()), sFrameHeaderBytes,
                    nonce.data(), mReceiveKey.data())
                != 0)
            {
                error = SecurityError::AuthenticationFailed;
                return false;
            }
            decoded.resize(static_cast<std::size_t>(decodedBytes));
            plaintext = std::move(decoded);
            // Only authenticated frames may advance the window. A forged future
            // counter must not evict valid in-flight messages or consume a slot.
            if (!mReceiveWindow.test(0))
            {
                mReceiveCounter = counter;
                mReceiveWindow.set(0);
            }
            else if (counter > mReceiveCounter)
            {
                const auto distance = counter - mReceiveCounter;
                if (distance >= replayWindowSize)
                    mReceiveWindow.reset();
                else
                    mReceiveWindow <<= static_cast<std::size_t>(distance);
                mReceiveCounter = counter;
                mReceiveWindow.set(0);
            }
            else
                mReceiveWindow.set(static_cast<std::size_t>(mReceiveCounter - counter));
            return true;
        }
        catch (const std::bad_alloc&)
        {
            error = SecurityError::AllocationFailed;
            return false;
        }
    }

    ClientHandshake::ClientHandshake()
    {
        if (!initializeSodium())
            return;
        crypto_kx_keypair(mHello.ephemeralPublicKey.data(), mSecretKey.data());
        randombytes_buf(mHello.nonce.data(), mHello.nonce.size());
        mInitialized = true;
    }

    ClientHandshake::~ClientHandshake()
    {
        sodium_memzero(mSecretKey.data(), mSecretKey.size());
    }

    bool ClientHandshake::finish(
        const ServerHello& response, SecureSession& session, SecurityError& error) noexcept
    {
        error = SecurityError::None;
        if (!mInitialized)
        {
            error = SecurityError::KeyExchangeFailed;
            return false;
        }
        if (mFinished)
        {
            error = SecurityError::ReplayDetected;
            return false;
        }
        const auto signedTranscript = transcript(mHello, response);
        if (crypto_sign_verify_detached(response.signature.data(), signedTranscript.data(),
                signedTranscript.size(), response.identityPublicKey.data())
            != 0)
        {
            error = SecurityError::InvalidSignature;
            return false;
        }

        std::array<unsigned char, crypto_kx_SESSIONKEYBYTES> receiveKey{};
        std::array<unsigned char, crypto_kx_SESSIONKEYBYTES> sendKey{};
        if (crypto_kx_client_session_keys(receiveKey.data(), sendKey.data(),
                mHello.ephemeralPublicKey.data(), mSecretKey.data(), response.ephemeralPublicKey.data())
            != 0)
        {
            error = SecurityError::KeyExchangeFailed;
            return false;
        }
        session = SecureSession(receiveKey, sendKey);
        sodium_memzero(receiveKey.data(), receiveKey.size());
        sodium_memzero(sendKey.data(), sendKey.size());
        sodium_memzero(mSecretKey.data(), mSecretKey.size());
        mFinished = true;
        return true;
    }

    ServerHandshake::ServerHandshake(const ServerIdentity& identity) noexcept
        : mIdentity(identity)
    {
    }

    bool ServerHandshake::accept(const ClientHello& hello, ServerHello& response,
        SecureSession& session, SecurityError& error) const noexcept
    {
        error = SecurityError::None;
        ServerHello generated;
        generated.identityPublicKey = mIdentity.publicKey();
        std::array<unsigned char, crypto_kx_SECRETKEYBYTES> ephemeralSecret{};
        crypto_kx_keypair(generated.ephemeralPublicKey.data(), ephemeralSecret.data());
        randombytes_buf(generated.nonce.data(), generated.nonce.size());

        const auto signedTranscript = transcript(hello, generated);
        crypto_sign_detached(generated.signature.data(), nullptr, signedTranscript.data(),
            signedTranscript.size(), mIdentity.secretKey().data());

        std::array<unsigned char, crypto_kx_SESSIONKEYBYTES> receiveKey{};
        std::array<unsigned char, crypto_kx_SESSIONKEYBYTES> sendKey{};
        const int result = crypto_kx_server_session_keys(receiveKey.data(), sendKey.data(),
            generated.ephemeralPublicKey.data(), ephemeralSecret.data(), hello.ephemeralPublicKey.data());
        sodium_memzero(ephemeralSecret.data(), ephemeralSecret.size());
        if (result != 0)
        {
            error = SecurityError::KeyExchangeFailed;
            return false;
        }

        session = SecureSession(receiveKey, sendKey);
        sodium_memzero(receiveKey.data(), receiveKey.size());
        sodium_memzero(sendKey.data(), sendKey.size());
        response = generated;
        return true;
    }

    bool encodeClientHello(const ClientHello& hello, std::vector<std::byte>& output) noexcept
    {
        protocol::PacketWriter writer(1 + hello.ephemeralPublicKey.size() + hello.nonce.size());
        writer.writeU8(sHandshakeVersion);
        writer.writeBytes(bytes(hello.ephemeralPublicKey));
        writer.writeBytes(bytes(hello.nonce));
        if (!writer.valid())
            return false;
        output = writer.take();
        return true;
    }

    bool decodeClientHello(std::span<const std::byte> input, ClientHello& hello) noexcept
    {
        ClientHello decoded;
        protocol::PacketReader reader(input);
        std::uint8_t version = 0;
        if (!reader.readU8(version) || version != sHandshakeVersion
            || !reader.readBytes(writableBytes(decoded.ephemeralPublicKey))
            || !reader.readBytes(writableBytes(decoded.nonce)) || !reader.finish())
            return false;
        hello = decoded;
        return true;
    }

    bool encodeServerHello(const ServerHello& hello, std::vector<std::byte>& output) noexcept
    {
        protocol::PacketWriter writer(1 + hello.identityPublicKey.size()
            + hello.ephemeralPublicKey.size() + hello.nonce.size() + hello.signature.size());
        writer.writeU8(sHandshakeVersion);
        writer.writeBytes(bytes(hello.identityPublicKey));
        writer.writeBytes(bytes(hello.ephemeralPublicKey));
        writer.writeBytes(bytes(hello.nonce));
        writer.writeBytes(bytes(hello.signature));
        if (!writer.valid())
            return false;
        output = writer.take();
        return true;
    }

    bool decodeServerHello(std::span<const std::byte> input, ServerHello& hello) noexcept
    {
        ServerHello decoded;
        protocol::PacketReader reader(input);
        std::uint8_t version = 0;
        if (!reader.readU8(version) || version != sHandshakeVersion
            || !reader.readBytes(writableBytes(decoded.identityPublicKey))
            || !reader.readBytes(writableBytes(decoded.ephemeralPublicKey))
            || !reader.readBytes(writableBytes(decoded.nonce))
            || !reader.readBytes(writableBytes(decoded.signature)) || !reader.finish())
            return false;
        hello = decoded;
        return true;
    }

    std::string fingerprint(const std::array<unsigned char, crypto_sign_PUBLICKEYBYTES>& publicKey)
    {
        std::array<char, sodium_base64_ENCODED_LEN(
                             crypto_sign_PUBLICKEYBYTES, sodium_base64_VARIANT_URLSAFE_NO_PADDING)>
            encoded{};
        sodium_bin2base64(encoded.data(), encoded.size(), publicKey.data(), publicKey.size(),
            sodium_base64_VARIANT_URLSAFE_NO_PADDING);
        return "ed25519/" + std::string(encoded.data());
    }
}
