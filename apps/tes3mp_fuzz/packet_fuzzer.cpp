#include <components/openmw-mp/Protocol/PacketCodec.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    const auto bytes = std::as_bytes(std::span(data, size));
    mwmp::protocol::ProtocolEnvelope envelope;
    std::span<const std::byte> payload;
    const auto result = mwmp::protocol::decodeMessage(bytes, envelope, payload);
    if (result)
    {
        mwmp::protocol::PacketReader reader(payload);
        std::string value;
        reader.readString(value);
        reader.finish();
    }

    mwmp::protocol::PacketReader arbitrary(bytes);
    std::uint64_t subject = 0;
    std::uint32_t count = 0;
    bool flag = false;
    std::string text;
    arbitrary.readU64(subject);
    arbitrary.readBool(flag);
    arbitrary.readCollectionCount(count);
    arbitrary.readString(text);
    arbitrary.finish();
    return 0;
}
