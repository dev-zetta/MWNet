#include "PasswordHash.hpp"

#include "SodiumInit.hpp"

#include <components/openmw-mp/Protocol/ProtocolLimits.hpp>

#include <algorithm>
#include <array>
#include <cstring>

#include <sodium.h>

namespace mwmp::security
{
    namespace
    {
        constexpr std::size_t sSha256HexBytes = crypto_hash_sha256_BYTES * 2U;
        constexpr std::size_t sMaximumLegacySaltBytes = 256;

        template <std::size_t Size>
        void sha256Hex(std::span<const unsigned char> input, std::array<char, Size>& output) noexcept
        {
            static_assert(Size == sSha256HexBytes);
            std::array<unsigned char, crypto_hash_sha256_BYTES> digest{};
            std::array<char, sSha256HexBytes + 1U> terminated{};
            crypto_hash_sha256(digest.data(), input.data(), input.size());
            sodium_bin2hex(terminated.data(), terminated.size(), digest.data(), digest.size());
            std::copy_n(terminated.begin(), output.size(), output.begin());
            sodium_memzero(terminated.data(), terminated.size());
            sodium_memzero(digest.data(), digest.size());
        }

        std::span<const unsigned char> bytes(std::span<const char> input) noexcept
        {
            return { reinterpret_cast<const unsigned char*>(input.data()), input.size() };
        }

        template <std::size_t Size>
        std::span<const unsigned char> bytes(const std::array<char, Size>& input) noexcept
        {
            return { reinterpret_cast<const unsigned char*>(input.data()), input.size() };
        }
    }

    PasswordBuffer::PasswordBuffer(unsigned char* data, std::size_t size) noexcept
        : mData(data)
        , mSize(size)
    {
    }

    std::optional<PasswordBuffer> PasswordBuffer::copyFrom(
        std::string_view password, std::string& error)
    {
        error.clear();
        if (password.empty() || password.size() > protocol::limits::passwordBytes)
        {
            error = "password must contain between 1 and 128 bytes";
            return std::nullopt;
        }
        if (!initializeSodium(&error))
            return std::nullopt;

        auto* data = static_cast<unsigned char*>(sodium_malloc(password.size()));
        if (data == nullptr)
        {
            error = "failed to allocate protected password memory";
            return std::nullopt;
        }
        std::memcpy(data, password.data(), password.size());
        return PasswordBuffer(data, password.size());
    }

    PasswordBuffer::PasswordBuffer(PasswordBuffer&& other) noexcept
        : mData(other.mData)
        , mSize(other.mSize)
    {
        other.mData = nullptr;
        other.mSize = 0;
    }

    PasswordBuffer& PasswordBuffer::operator=(PasswordBuffer&& other) noexcept
    {
        if (this != &other)
        {
            clear();
            mData = other.mData;
            mSize = other.mSize;
            other.mData = nullptr;
            other.mSize = 0;
        }
        return *this;
    }

    PasswordBuffer::~PasswordBuffer()
    {
        clear();
    }

    std::span<const char> PasswordBuffer::characters() const noexcept
    {
        return { reinterpret_cast<const char*>(mData), mSize };
    }

    void PasswordBuffer::clear() noexcept
    {
        if (mData != nullptr)
            sodium_free(mData);
        mData = nullptr;
        mSize = 0;
    }

    bool PasswordHash::createArgon2id(
        const PasswordBuffer& password, std::string& encoded, std::string& error)
    {
        error.clear();
        if (!initializeSodium(&error) || password.size() == 0
            || password.size() > protocol::limits::passwordBytes)
        {
            if (error.empty())
                error = "invalid password length";
            return false;
        }

        std::array<char, crypto_pwhash_STRBYTES> output{};
        const auto characters = password.characters();
        if (crypto_pwhash_str_alg(output.data(), characters.data(), characters.size(),
                crypto_pwhash_OPSLIMIT_MODERATE, crypto_pwhash_MEMLIMIT_MODERATE,
                crypto_pwhash_ALG_ARGON2ID13)
            != 0)
        {
            error = "Argon2id hashing failed because its memory allocation was refused";
            return false;
        }
        encoded.assign(output.data());
        sodium_memzero(output.data(), output.size());
        return true;
    }

    bool PasswordHash::verifyArgon2id(
        const PasswordBuffer& password, std::string_view encoded) noexcept
    {
        if (!initializeSodium() || password.size() == 0
            || password.size() > protocol::limits::passwordBytes
            || encoded.empty() || encoded.size() >= crypto_pwhash_STRBYTES
            || !encoded.starts_with("$argon2id$"))
            return false;

        std::array<char, crypto_pwhash_STRBYTES> terminated{};
        std::copy(encoded.begin(), encoded.end(), terminated.begin());
        const auto characters = password.characters();
        const bool valid = crypto_pwhash_str_verify(
            terminated.data(), characters.data(), characters.size()) == 0;
        sodium_memzero(terminated.data(), terminated.size());
        return valid;
    }

    bool PasswordHash::needsRehash(std::string_view encoded) noexcept
    {
        if (!initializeSodium() || encoded.empty() || encoded.size() >= crypto_pwhash_STRBYTES
            || !encoded.starts_with("$argon2id$"))
            return true;
        std::array<char, crypto_pwhash_STRBYTES> terminated{};
        std::copy(encoded.begin(), encoded.end(), terminated.begin());
        const bool result = crypto_pwhash_str_needs_rehash(terminated.data(),
            crypto_pwhash_OPSLIMIT_MODERATE, crypto_pwhash_MEMLIMIT_MODERATE) != 0;
        sodium_memzero(terminated.data(), terminated.size());
        return result;
    }

    bool PasswordHash::verifyLegacySha256(const PasswordBuffer& password,
        std::string_view salt, std::string_view encodedHash) noexcept
    {
        if (!initializeSodium() || password.size() == 0
            || password.size() > protocol::limits::passwordBytes || salt.empty()
            || salt.size() > sMaximumLegacySaltBytes || encodedHash.size() != sSha256HexBytes)
            return false;

        std::array<unsigned char, crypto_hash_sha256_BYTES> expected{};
        std::size_t expectedBytes = 0;
        if (sodium_hex2bin(expected.data(), expected.size(), encodedHash.data(), encodedHash.size(),
                nullptr, &expectedBytes, nullptr)
                != 0
            || expectedBytes != expected.size())
            return false;

        std::array<char, sSha256HexBytes> first{};
        std::array<char, sSha256HexBytes> second{};
        std::array<char, sSha256HexBytes> third{};
        std::array<char, sSha256HexBytes> clientHash{};
        sha256Hex(bytes(password.characters()), first);
        sha256Hex(bytes(first), second);
        sha256Hex(bytes(second), third);

        std::array<unsigned char, sSha256HexBytes * 2U> clientInput{};
        std::memcpy(clientInput.data(), first.data(), first.size());
        std::memcpy(clientInput.data() + first.size(), third.data(), third.size());
        sha256Hex(clientInput, clientHash);

        std::array<unsigned char, sSha256HexBytes + sMaximumLegacySaltBytes> serverInput{};
        std::memcpy(serverInput.data(), clientHash.data(), clientHash.size());
        std::memcpy(serverInput.data() + clientHash.size(), salt.data(), salt.size());
        std::array<unsigned char, crypto_hash_sha256_BYTES> actual{};
        crypto_hash_sha256(actual.data(), serverInput.data(), clientHash.size() + salt.size());
        const bool valid = sodium_memcmp(actual.data(), expected.data(), actual.size()) == 0;

        sodium_memzero(first.data(), first.size());
        sodium_memzero(second.data(), second.size());
        sodium_memzero(third.data(), third.size());
        sodium_memzero(clientHash.data(), clientHash.size());
        sodium_memzero(clientInput.data(), clientInput.size());
        sodium_memzero(serverInput.data(), serverInput.size());
        sodium_memzero(actual.data(), actual.size());
        sodium_memzero(expected.data(), expected.size());
        return valid;
    }
}
