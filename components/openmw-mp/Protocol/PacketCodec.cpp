#include "PacketCodec.hpp"

#include <array>
#include <bit>
#include <cstring>
#include <limits>
#include <new>

namespace mwmp::protocol
{
    namespace
    {
        constexpr std::array<std::byte, 4> sMagic{ std::byte{ 'T' }, std::byte{ '3' }, std::byte{ 'M' },
            std::byte{ 'P' } };

        std::size_t maximumPayload(std::uint16_t flags) noexcept
        {
            return (flags & envelopeFlagBulkChunk) != 0 ? limits::bulkChunkBytes : limits::normalMessageBytes;
        }
    }

    PacketReader::PacketReader(std::span<const std::byte> bytes) noexcept
        : mBytes(bytes)
    {
    }

    bool PacketReader::fail(CodecError error) noexcept
    {
        if (mError == CodecError::None)
            mError = error;
        return false;
    }

    bool PacketReader::readU8(std::uint8_t& value) noexcept
    {
        return readUnsigned(value);
    }

    bool PacketReader::readU16(std::uint16_t& value) noexcept
    {
        return readUnsigned(value);
    }

    bool PacketReader::readU32(std::uint32_t& value) noexcept
    {
        return readUnsigned(value);
    }

    bool PacketReader::readU64(std::uint64_t& value) noexcept
    {
        return readUnsigned(value);
    }

    bool PacketReader::readI8(std::int8_t& value) noexcept
    {
        std::uint8_t decoded = 0;
        if (!readU8(decoded))
            return false;
        value = std::bit_cast<std::int8_t>(decoded);
        return true;
    }

    bool PacketReader::readI16(std::int16_t& value) noexcept
    {
        std::uint16_t decoded = 0;
        if (!readU16(decoded))
            return false;
        value = std::bit_cast<std::int16_t>(decoded);
        return true;
    }

    bool PacketReader::readI32(std::int32_t& value) noexcept
    {
        std::uint32_t decoded = 0;
        if (!readU32(decoded))
            return false;
        value = std::bit_cast<std::int32_t>(decoded);
        return true;
    }

    bool PacketReader::readI64(std::int64_t& value) noexcept
    {
        std::uint64_t decoded = 0;
        if (!readU64(decoded))
            return false;
        value = std::bit_cast<std::int64_t>(decoded);
        return true;
    }

    bool PacketReader::readFloat(float& value) noexcept
    {
        static_assert(sizeof(float) == sizeof(std::uint32_t));
        std::uint32_t decoded = 0;
        if (!readU32(decoded))
            return false;
        value = std::bit_cast<float>(decoded);
        return true;
    }

    bool PacketReader::readDouble(double& value) noexcept
    {
        static_assert(sizeof(double) == sizeof(std::uint64_t));
        std::uint64_t decoded = 0;
        if (!readU64(decoded))
            return false;
        value = std::bit_cast<double>(decoded);
        return true;
    }

    bool PacketReader::readBool(bool& value) noexcept
    {
        std::uint8_t decoded = 0;
        if (!readU8(decoded))
            return false;
        if (decoded > 1)
            return fail(CodecError::InvalidValue);
        value = decoded != 0;
        return true;
    }

    bool PacketReader::readString(std::string& value, std::size_t maximumBytes) noexcept
    {
        std::uint32_t length = 0;
        const std::size_t initialPosition = mPosition;
        if (!readU32(length))
            return false;
        if (length > maximumBytes)
        {
            mPosition = initialPosition;
            return fail(CodecError::LimitExceeded);
        }
        if (remaining() < length)
        {
            mPosition = initialPosition;
            return fail(CodecError::Truncated);
        }

        const auto encoded = mBytes.subspan(mPosition, length);
        if (!isValidUtf8(encoded))
        {
            mPosition = initialPosition;
            return fail(CodecError::InvalidUtf8);
        }

        try
        {
            std::string decoded(reinterpret_cast<const char*>(encoded.data()), encoded.size());
            value = std::move(decoded);
        }
        catch (const std::bad_alloc&)
        {
            mPosition = initialPosition;
            return fail(CodecError::AllocationFailed);
        }

        mPosition += length;
        return true;
    }

    bool PacketReader::readCollectionCount(std::uint32_t& value, std::uint32_t maximumElements) noexcept
    {
        std::uint32_t decoded = 0;
        if (!readU32(decoded))
            return false;
        if (decoded > maximumElements)
            return fail(CodecError::LimitExceeded);
        value = decoded;
        return true;
    }

    bool PacketReader::readBytes(std::span<std::byte> destination) noexcept
    {
        if (mError != CodecError::None)
            return false;
        if (remaining() < destination.size())
            return fail(CodecError::Truncated);
        if (destination.empty())
            return true;

        std::memcpy(destination.data(), mBytes.data() + mPosition, destination.size());
        mPosition += destination.size();
        return true;
    }

    bool PacketReader::finish() noexcept
    {
        if (mError != CodecError::None)
            return false;
        if (mPosition != mBytes.size())
            return fail(CodecError::TrailingData);
        return true;
    }

    DecodeResult PacketReader::result() const noexcept
    {
        return { mError, mPosition };
    }

    PacketWriter::PacketWriter(std::size_t maximumBytes) noexcept
        : mMaximumBytes(maximumBytes)
    {
    }

    bool PacketWriter::fail(CodecError error) noexcept
    {
        if (mError == CodecError::None)
            mError = error;
        return false;
    }

    bool PacketWriter::reserveFor(std::size_t additionalBytes) noexcept
    {
        if (mError != CodecError::None)
            return false;
        if (additionalBytes > mMaximumBytes - mBytes.size())
            return fail(CodecError::LimitExceeded);

        try
        {
            mBytes.reserve(mBytes.size() + additionalBytes);
        }
        catch (const std::bad_alloc&)
        {
            return fail(CodecError::AllocationFailed);
        }
        return true;
    }

    bool PacketWriter::writeU8(std::uint8_t value) noexcept
    {
        return writeUnsigned(value);
    }

    bool PacketWriter::writeU16(std::uint16_t value) noexcept
    {
        return writeUnsigned(value);
    }

    bool PacketWriter::writeU32(std::uint32_t value) noexcept
    {
        return writeUnsigned(value);
    }

    bool PacketWriter::writeU64(std::uint64_t value) noexcept
    {
        return writeUnsigned(value);
    }

    bool PacketWriter::writeI8(std::int8_t value) noexcept
    {
        return writeU8(std::bit_cast<std::uint8_t>(value));
    }

    bool PacketWriter::writeI16(std::int16_t value) noexcept
    {
        return writeU16(std::bit_cast<std::uint16_t>(value));
    }

    bool PacketWriter::writeI32(std::int32_t value) noexcept
    {
        return writeU32(std::bit_cast<std::uint32_t>(value));
    }

    bool PacketWriter::writeI64(std::int64_t value) noexcept
    {
        return writeU64(std::bit_cast<std::uint64_t>(value));
    }

    bool PacketWriter::writeFloat(float value) noexcept
    {
        return writeU32(std::bit_cast<std::uint32_t>(value));
    }

    bool PacketWriter::writeDouble(double value) noexcept
    {
        return writeU64(std::bit_cast<std::uint64_t>(value));
    }

    bool PacketWriter::writeBool(bool value) noexcept
    {
        return writeU8(value ? 1 : 0);
    }

    bool PacketWriter::writeString(std::string_view value, std::size_t maximumBytes) noexcept
    {
        if (value.size() > maximumBytes || value.size() > std::numeric_limits<std::uint32_t>::max())
            return fail(CodecError::LimitExceeded);
        if (!isValidUtf8(std::as_bytes(std::span(value))))
            return fail(CodecError::InvalidUtf8);
        if (!reserveFor(sizeof(std::uint32_t) + value.size()))
            return false;

        writeU32(static_cast<std::uint32_t>(value.size()));
        for (const char character : value)
            mBytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
        return true;
    }

    bool PacketWriter::writeCollectionCount(std::uint32_t value, std::uint32_t maximumElements) noexcept
    {
        if (value > maximumElements)
            return fail(CodecError::LimitExceeded);
        return writeU32(value);
    }

    bool PacketWriter::writeBytes(std::span<const std::byte> value) noexcept
    {
        if (!reserveFor(value.size()))
            return false;
        mBytes.insert(mBytes.end(), value.begin(), value.end());
        return true;
    }

    bool encodeMessage(const ProtocolEnvelope& envelope, std::span<const std::byte> payload,
        std::vector<std::byte>& destination, CodecError& error) noexcept
    {
        error = CodecError::None;
        if (envelope.messageType == 0 || (envelope.flags & ~envelopeKnownFlags) != 0)
        {
            error = CodecError::InvalidValue;
            return false;
        }
        if (payload.size() > maximumPayload(envelope.flags)
            || payload.size() > std::numeric_limits<std::uint32_t>::max())
        {
            error = CodecError::LimitExceeded;
            return false;
        }

        PacketWriter writer(envelopeBytes + maximumPayload(envelope.flags));
        writer.writeBytes(sMagic);
        writer.writeU16(protocolVersion);
        writer.writeU16(envelope.messageType);
        writer.writeU16(envelope.flags);
        writer.writeU16(0);
        writer.writeU32(envelope.sequence);
        writer.writeU64(envelope.subjectId);
        writer.writeU32(static_cast<std::uint32_t>(payload.size()));
        writer.writeBytes(payload);
        if (!writer.valid())
        {
            error = writer.error();
            return false;
        }

        destination = writer.take();
        return true;
    }

    DecodeResult decodeMessage(std::span<const std::byte> message, ProtocolEnvelope& envelope,
        std::span<const std::byte>& payload) noexcept
    {
        ProtocolEnvelope decoded;
        std::span<const std::byte> decodedPayload;
        PacketReader reader(message);

        std::array<std::byte, sMagic.size()> magic{};
        if (!reader.readBytes(magic))
            return reader.result();
        if (magic != sMagic)
            return { CodecError::InvalidMagic, reader.position() };

        std::uint16_t version = 0;
        std::uint16_t reserved = 0;
        std::uint32_t payloadSize = 0;
        if (!reader.readU16(version) || !reader.readU16(decoded.messageType) || !reader.readU16(decoded.flags)
            || !reader.readU16(reserved) || !reader.readU32(decoded.sequence) || !reader.readU64(decoded.subjectId)
            || !reader.readU32(payloadSize))
            return reader.result();

        if (version != protocolVersion)
            return { CodecError::UnsupportedVersion, reader.position() };
        if (decoded.messageType == 0 || reserved != 0 || (decoded.flags & ~envelopeKnownFlags) != 0)
            return { CodecError::InvalidValue, reader.position() };
        if (payloadSize > maximumPayload(decoded.flags))
            return { CodecError::LimitExceeded, reader.position() };
        if (reader.remaining() < payloadSize)
            return { CodecError::Truncated, reader.position() };
        if (reader.remaining() > payloadSize)
            return { CodecError::TrailingData, reader.position() + payloadSize };

        decodedPayload = message.subspan(reader.position(), payloadSize);
        envelope = decoded;
        payload = decodedPayload;
        return { CodecError::None, message.size() };
    }

    bool isValidUtf8(std::span<const std::byte> value) noexcept
    {
        std::size_t position = 0;
        while (position < value.size())
        {
            const auto first = std::to_integer<std::uint8_t>(value[position]);
            if (first <= 0x7f)
            {
                ++position;
                continue;
            }

            std::size_t continuationCount = 0;
            std::uint32_t codePoint = 0;
            std::uint32_t minimum = 0;
            if ((first & 0xe0U) == 0xc0U)
            {
                continuationCount = 1;
                codePoint = first & 0x1fU;
                minimum = 0x80;
            }
            else if ((first & 0xf0U) == 0xe0U)
            {
                continuationCount = 2;
                codePoint = first & 0x0fU;
                minimum = 0x800;
            }
            else if ((first & 0xf8U) == 0xf0U)
            {
                continuationCount = 3;
                codePoint = first & 0x07U;
                minimum = 0x10000;
            }
            else
                return false;

            if (value.size() - position - 1 < continuationCount)
                return false;
            for (std::size_t offset = 1; offset <= continuationCount; ++offset)
            {
                const auto continuation = std::to_integer<std::uint8_t>(value[position + offset]);
                if ((continuation & 0xc0U) != 0x80U)
                    return false;
                codePoint = (codePoint << 6U) | (continuation & 0x3fU);
            }

            if (codePoint < minimum || codePoint > 0x10ffffU
                || (codePoint >= 0xd800U && codePoint <= 0xdfffU))
                return false;
            position += continuationCount + 1;
        }
        return true;
    }

    const char* describe(CodecError error) noexcept
    {
        switch (error)
        {
            case CodecError::None:
                return "no error";
            case CodecError::Truncated:
                return "truncated message";
            case CodecError::InvalidMagic:
                return "invalid protocol magic";
            case CodecError::UnsupportedVersion:
                return "unsupported protocol version";
            case CodecError::InvalidValue:
                return "invalid field value";
            case CodecError::InvalidUtf8:
                return "invalid UTF-8";
            case CodecError::LimitExceeded:
                return "protocol limit exceeded";
            case CodecError::TrailingData:
                return "unexpected trailing data";
            case CodecError::AllocationFailed:
                return "allocation failed";
        }
        return "unknown codec error";
    }
}
