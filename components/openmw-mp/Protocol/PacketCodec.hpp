#ifndef OPENMW_MP_PACKET_CODEC_HPP
#define OPENMW_MP_PACKET_CODEC_HPP

#include "ProtocolLimits.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace mwmp::protocol
{
    enum class CodecError
    {
        None,
        Truncated,
        InvalidMagic,
        UnsupportedVersion,
        InvalidValue,
        InvalidUtf8,
        LimitExceeded,
        TrailingData,
        AllocationFailed,
    };

    struct DecodeResult
    {
        CodecError error = CodecError::None;
        std::size_t consumed = 0;

        explicit operator bool() const noexcept { return error == CodecError::None; }
    };

    class PacketReader
    {
    public:
        explicit PacketReader(std::span<const std::byte> bytes) noexcept;

        bool readU8(std::uint8_t& value) noexcept;
        bool readU16(std::uint16_t& value) noexcept;
        bool readU32(std::uint32_t& value) noexcept;
        bool readU64(std::uint64_t& value) noexcept;
        bool readI8(std::int8_t& value) noexcept;
        bool readI16(std::int16_t& value) noexcept;
        bool readI32(std::int32_t& value) noexcept;
        bool readI64(std::int64_t& value) noexcept;
        bool readFloat(float& value) noexcept;
        bool readDouble(double& value) noexcept;
        bool readBool(bool& value) noexcept;
        bool readString(std::string& value, std::size_t maximumBytes = limits::defaultStringBytes) noexcept;
        bool readCollectionCount(
            std::uint32_t& value, std::uint32_t maximumElements = limits::defaultCollectionElements) noexcept;
        bool readBytes(std::span<std::byte> destination) noexcept;

        bool finish() noexcept;
        DecodeResult result() const noexcept;
        CodecError error() const noexcept { return mError; }
        std::size_t position() const noexcept { return mPosition; }
        std::size_t remaining() const noexcept { return mBytes.size() - mPosition; }

    private:
        template <class Unsigned>
        bool readUnsigned(Unsigned& value) noexcept
        {
            static_assert(std::is_unsigned_v<Unsigned>);
            if (mError != CodecError::None)
                return false;
            if (remaining() < sizeof(Unsigned))
                return fail(CodecError::Truncated);

            Unsigned decoded = 0;
            for (std::size_t index = 0; index < sizeof(Unsigned); ++index)
                decoded |= static_cast<Unsigned>(std::to_integer<std::uint8_t>(mBytes[mPosition + index]))
                    << (index * 8U);

            mPosition += sizeof(Unsigned);
            value = decoded;
            return true;
        }

        bool fail(CodecError error) noexcept;

        std::span<const std::byte> mBytes;
        std::size_t mPosition = 0;
        CodecError mError = CodecError::None;
    };

    class PacketWriter
    {
    public:
        explicit PacketWriter(std::size_t maximumBytes = limits::normalMessageBytes) noexcept;

        bool writeU8(std::uint8_t value) noexcept;
        bool writeU16(std::uint16_t value) noexcept;
        bool writeU32(std::uint32_t value) noexcept;
        bool writeU64(std::uint64_t value) noexcept;
        bool writeI8(std::int8_t value) noexcept;
        bool writeI16(std::int16_t value) noexcept;
        bool writeI32(std::int32_t value) noexcept;
        bool writeI64(std::int64_t value) noexcept;
        bool writeFloat(float value) noexcept;
        bool writeDouble(double value) noexcept;
        bool writeBool(bool value) noexcept;
        bool writeString(std::string_view value, std::size_t maximumBytes = limits::defaultStringBytes) noexcept;
        bool writeCollectionCount(
            std::uint32_t value, std::uint32_t maximumElements = limits::defaultCollectionElements) noexcept;
        bool writeBytes(std::span<const std::byte> value) noexcept;

        CodecError error() const noexcept { return mError; }
        bool valid() const noexcept { return mError == CodecError::None; }
        std::span<const std::byte> bytes() const noexcept { return mBytes; }
        const std::vector<std::byte>& storage() const noexcept { return mBytes; }
        std::vector<std::byte> take() noexcept { return std::move(mBytes); }

    private:
        template <class Unsigned>
        bool writeUnsigned(Unsigned value) noexcept
        {
            static_assert(std::is_unsigned_v<Unsigned>);
            if (!reserveFor(sizeof(Unsigned)))
                return false;

            for (std::size_t index = 0; index < sizeof(Unsigned); ++index)
                mBytes.push_back(static_cast<std::byte>((value >> (index * 8U)) & 0xffU));
            return true;
        }

        bool reserveFor(std::size_t additionalBytes) noexcept;
        bool fail(CodecError error) noexcept;

        std::vector<std::byte> mBytes;
        std::size_t mMaximumBytes;
        CodecError mError = CodecError::None;
    };

    inline constexpr std::uint16_t protocolVersion = 12;
    inline constexpr std::uint16_t envelopeFlagBulkChunk = 1U << 0U;
    inline constexpr std::uint16_t envelopeFlagUnreliable = 1U << 1U;
    inline constexpr std::uint16_t envelopeKnownFlags = envelopeFlagBulkChunk | envelopeFlagUnreliable;
    inline constexpr std::size_t envelopeBytes = 32;

    struct ProtocolEnvelope
    {
        std::uint16_t messageType = 0;
        std::uint64_t subjectId = 0;
        std::uint64_t sequence = 0;
        std::uint16_t flags = 0;

        bool operator==(const ProtocolEnvelope&) const = default;
    };

    bool encodeMessage(const ProtocolEnvelope& envelope, std::span<const std::byte> payload,
        std::vector<std::byte>& destination, CodecError& error) noexcept;
    DecodeResult decodeMessage(std::span<const std::byte> message, ProtocolEnvelope& envelope,
        std::span<const std::byte>& payload) noexcept;

    bool isValidUtf8(std::span<const std::byte> value) noexcept;
    const char* describe(CodecError error) noexcept;
}

#endif
