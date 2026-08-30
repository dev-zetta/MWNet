#ifndef OPENMW_MP_SECURITY_PASSWORD_HASH_HPP
#define OPENMW_MP_SECURITY_PASSWORD_HASH_HPP

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace mwmp::security
{
    class PasswordBuffer
    {
    public:
        static std::optional<PasswordBuffer> copyFrom(
            std::string_view password, std::string& error);

        PasswordBuffer(PasswordBuffer&& other) noexcept;
        PasswordBuffer& operator=(PasswordBuffer&& other) noexcept;
        ~PasswordBuffer();

        PasswordBuffer(const PasswordBuffer&) = delete;
        PasswordBuffer& operator=(const PasswordBuffer&) = delete;

        std::span<const char> characters() const noexcept;
        std::size_t size() const noexcept { return mSize; }

    private:
        PasswordBuffer(unsigned char* data, std::size_t size) noexcept;
        void clear() noexcept;

        unsigned char* mData = nullptr;
        std::size_t mSize = 0;
    };

    class PasswordHash
    {
    public:
        static bool createArgon2id(
            const PasswordBuffer& password, std::string& encoded, std::string& error);
        static bool verifyArgon2id(
            const PasswordBuffer& password, std::string_view encoded) noexcept;
        static bool needsRehash(std::string_view encoded) noexcept;
        static bool verifyLegacySha256(const PasswordBuffer& password,
            std::string_view salt, std::string_view encodedHash) noexcept;
    };
}

#endif
