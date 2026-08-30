#include <components/openmw-mp/Protocol/EndpointSecurity.hpp>
#include <components/openmw-mp/Protocol/PacketCodec.hpp>
#include <components/openmw-mp/Protocol/RateLimits.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <vector>

namespace
{
    using namespace mwmp::protocol;

    int sFailures = 0;

    void expect(bool condition, const char* expression, int line)
    {
        if (condition)
            return;
        std::cerr << "protocol.cpp:" << line << ": expectation failed: " << expression << '\n';
        ++sFailures;
    }

#define EXPECT(condition) expect((condition), #condition, __LINE__)

    std::span<const std::byte> asBytes(std::string_view value)
    {
        return std::as_bytes(std::span(value));
    }

    void testPrimitiveRoundTrip()
    {
        PacketWriter writer;
        EXPECT(writer.writeU16(0x1234));
        EXPECT(writer.writeU32(0x89abcdef));
        EXPECT(writer.writeI64(-42));
        EXPECT(writer.writeFloat(1.5f));
        EXPECT(writer.writeBool(true));
        EXPECT(writer.writeString("Vvardenfell"));
        EXPECT(writer.writeCollectionCount(3000, limits::actorChanges));
        EXPECT(writer.storage()[0] == std::byte{ 0x34 });
        EXPECT(writer.storage()[1] == std::byte{ 0x12 });

        PacketReader reader(writer.bytes());
        std::uint16_t u16 = 0;
        std::uint32_t u32 = 0;
        std::int64_t i64 = 0;
        float real = 0;
        bool boolean = false;
        std::string text;
        std::uint32_t count = 0;
        EXPECT(reader.readU16(u16) && u16 == 0x1234);
        EXPECT(reader.readU32(u32) && u32 == 0x89abcdef);
        EXPECT(reader.readI64(i64) && i64 == -42);
        EXPECT(reader.readFloat(real) && real == 1.5f);
        EXPECT(reader.readBool(boolean) && boolean);
        EXPECT(reader.readString(text) && text == "Vvardenfell");
        EXPECT(reader.readCollectionCount(count, limits::actorChanges) && count == 3000);
        EXPECT(reader.finish());
    }

    void testReadFailureIsStickyAndNonMutating()
    {
        const std::array bytes{ std::byte{ 0x34 } };
        PacketReader reader(bytes);
        std::uint16_t value = 0xbeef;
        EXPECT(!reader.readU16(value));
        EXPECT(value == 0xbeef);
        EXPECT(reader.error() == CodecError::Truncated);

        std::uint8_t second = 0xaa;
        EXPECT(!reader.readU8(second));
        EXPECT(second == 0xaa);
        EXPECT(reader.error() == CodecError::Truncated);
    }

    void testStrings()
    {
        PacketWriter writer;
        EXPECT(writer.writeString("Balmora \xE2\x98\x94"));
        PacketReader reader(writer.bytes());
        std::string decoded = "unchanged";
        EXPECT(reader.readString(decoded));
        EXPECT(decoded == "Balmora \xE2\x98\x94");

        const std::array invalid{ std::byte{ 2 }, std::byte{ 0 }, std::byte{ 0 }, std::byte{ 0 },
            std::byte{ 0xc0 }, std::byte{ 0xaf } };
        PacketReader invalidReader(invalid);
        decoded = "unchanged";
        EXPECT(!invalidReader.readString(decoded));
        EXPECT(invalidReader.error() == CodecError::InvalidUtf8);
        EXPECT(decoded == "unchanged");

        const std::array invalidBool{ std::byte{ 2 } };
        PacketReader boolReader(invalidBool);
        bool boolValue = true;
        EXPECT(!boolReader.readBool(boolValue));
        EXPECT(boolReader.error() == CodecError::InvalidValue);
        EXPECT(boolValue);

        PacketWriter limited;
        EXPECT(!limited.writeString("12345", 4));
        EXPECT(limited.error() == CodecError::LimitExceeded);
        EXPECT(limited.storage().empty());
    }

    void testEnvelopeRoundTripAndTruncation()
    {
        const ProtocolEnvelope expected{ 42, 0x123456789abcdef0ULL, 91, envelopeFlagUnreliable };
        const auto originalPayload = asBytes("payload");
        std::vector<std::byte> message;
        CodecError encodeError = CodecError::InvalidValue;
        EXPECT(encodeMessage(expected, originalPayload, message, encodeError));
        EXPECT(encodeError == CodecError::None);
        EXPECT(message.size() == envelopeBytes + originalPayload.size());

        ProtocolEnvelope decoded{ 99, 1, 2, 3 };
        std::span<const std::byte> payload;
        const DecodeResult result = decodeMessage(message, decoded, payload);
        EXPECT(static_cast<bool>(result));
        EXPECT(decoded == expected);
        EXPECT(payload.size() == originalPayload.size());
        EXPECT(std::equal(payload.begin(), payload.end(), originalPayload.begin()));

        for (std::size_t length = 0; length < message.size(); ++length)
        {
            ProtocolEnvelope sentinel{ 99, 1, 2, 3 };
            std::span<const std::byte> sentinelPayload = asBytes("unchanged");
            const DecodeResult truncated = decodeMessage(std::span(message).first(length), sentinel, sentinelPayload);
            EXPECT(!static_cast<bool>(truncated));
            EXPECT((sentinel == ProtocolEnvelope{ 99, 1, 2, 3 }));
            EXPECT(sentinelPayload.size() == 9);
        }

        message.push_back(std::byte{ 0 });
        EXPECT(decodeMessage(message, decoded, payload).error == CodecError::TrailingData);

        message.pop_back();
        message[4] = std::byte{ 10 };
        EXPECT(decodeMessage(message, decoded, payload).error == CodecError::UnsupportedVersion);

        message[4] = std::byte{ 11 };
        message[8] = std::byte{ 0x80 };
        EXPECT(decodeMessage(message, decoded, payload).error == CodecError::InvalidValue);
    }

    void testEnvelopeLimits()
    {
        const ProtocolEnvelope normal{ 1, 0, 0, 0 };
        std::vector<std::byte> destination{ std::byte{ 0x7f } };
        CodecError error = CodecError::None;
        const std::vector<std::byte> oversized(limits::normalMessageBytes + 1);
        EXPECT(!encodeMessage(normal, oversized, destination, error));
        EXPECT(error == CodecError::LimitExceeded);
        EXPECT(destination == std::vector<std::byte>{ std::byte{ 0x7f } });

        const ProtocolEnvelope bulk{ 1, 0, 0, envelopeFlagBulkChunk };
        const std::vector<std::byte> oversizedChunk(limits::bulkChunkBytes + 1);
        EXPECT(!encodeMessage(bulk, oversizedChunk, destination, error));
        EXPECT(error == CodecError::LimitExceeded);
    }

    void testRateLimits()
    {
        using Clock = TokenBucket::Clock;
        const auto start = Clock::time_point{};
        TokenBucket bucket(2.0, 4.0, start);
        EXPECT(bucket.consume(4.0, start));
        EXPECT(!bucket.consume(0.1, start));
        EXPECT(bucket.consume(2.0, start + std::chrono::seconds(1)));

        ConnectionRateLimiter connection(start);
        for (std::size_t index = 0; index < static_cast<std::size_t>(limits::messageBurst); ++index)
            EXPECT(connection.consume(1, start));
        EXPECT(!connection.consume(1, start));
        EXPECT(connection.consume(1, start + std::chrono::seconds(1)));

        ChatRateLimiter chat;
        for (std::size_t index = 0; index < limits::chatMessagesPerWindow; ++index)
            EXPECT(chat.consume(start));
        EXPECT(!chat.consume(start + std::chrono::seconds(9)));
        EXPECT(chat.consume(start + std::chrono::seconds(10)));
    }

    void testListenAddressPolicy()
    {
        EXPECT(isLoopbackAddress("127.0.0.1"));
        EXPECT(isLoopbackAddress("127.255.255.255"));
        EXPECT(isLoopbackAddress("::1"));
        EXPECT(!isLoopbackAddress("127.0.0"));
        EXPECT(!isLoopbackAddress("127.0.0.1.example"));
        EXPECT(!isLoopbackAddress("0.0.0.0"));
        EXPECT(!isLoopbackAddress("192.168.1.2"));
        EXPECT(isListenAddressAllowed("127.0.0.1", false));
        EXPECT(!isListenAddressAllowed("0.0.0.0", false));
        EXPECT(isListenAddressAllowed("0.0.0.0", true));
    }
}

int runProtocolTests()
{
    testPrimitiveRoundTrip();
    testReadFailureIsStickyAndNonMutating();
    testStrings();
    testEnvelopeRoundTripAndTruncation();
    testEnvelopeLimits();
    testRateLimits();
    testListenAddressPolicy();
    return sFailures;
}
