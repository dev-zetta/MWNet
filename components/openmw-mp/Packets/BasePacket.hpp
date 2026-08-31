#ifndef OPENMW_BASEPACKET_HPP
#define OPENMW_BASEPACKET_HPP

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <BitStream.h>
#include <components/esm/path.hpp>
#include <components/esm/position.hpp>
#include <components/esm/refid.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/esm3/statstate.hpp>
#include <components/openmw-mp/Protocol/PacketCodec.hpp>
#include <components/openmw-mp/Transport/ApplicationPacketFrame.hpp>

namespace mwmp::transport
{
    class ApplicationPacketDispatcher;
}


namespace mwmp
{
    class BasePacket
    {
    public:
        explicit BasePacket();

        virtual ~BasePacket() = default;

        virtual void Packet(RakNet::BitStream *newBitstream, bool send);
        virtual uint32_t Send(bool toOtherPlayers = true);
        virtual uint32_t Send(transport::TransportConnectionId destination);
        virtual void Read();

        void setGUID(mwmp::transport::TransportConnectionId newGuid);
        mwmp::transport::TransportConnectionId getGUID();

        void SetReadStream(RakNet::BitStream *bitStream);
        void SetSendStream(RakNet::BitStream *bitStream);
        void SetStreams(RakNet::BitStream *inStream, RakNet::BitStream *outStream);
        void SetApplicationPacketDispatcher(transport::ApplicationPacketDispatcher* dispatcher);
        virtual uint32_t RequestData(mwmp::transport::TransportConnectionId targetGuid);

        static inline uint32_t headerSize()
        {
            return 1U + sizeof(std::uint64_t);
        }

        uint8_t GetPacketID() const
        {
            return packetID;
        }

        bool isPacketValid() const
        {
            return packetValid;
        }

        protocol::CodecError getCodecError() const
        {
            return codecError;
        }

    protected:
        template <class>
        static constexpr bool unsupportedPacketType = false;

        template<class Value>
        bool RW(Value& data, bool write, bool compress = false)
        {
            (void)compress;
            if (!packetValid)
                return false;

            using Type = std::remove_cv_t<Value>;
            if constexpr (std::is_same_v<Type, bool>)
            {
                bool decoded = false;
                if (write)
                    return writeResult(mWriter && mWriter->writeBool(data));
                if (!readResult(mReader && mReader->readBool(decoded)))
                    return false;
                data = decoded;
                return true;
            }
            else if constexpr (std::is_enum_v<Type>)
            {
                using Underlying = std::underlying_type_t<Type>;
                Underlying value = static_cast<Underlying>(data);
                if (!RW(value, write))
                    return false;
                if (!write)
                    data = static_cast<Type>(value);
                return true;
            }
            else if constexpr (std::is_integral_v<Type>)
            {
                bool result = false;
                if constexpr (std::is_signed_v<Type>)
                {
                    if constexpr (sizeof(Type) == sizeof(std::int8_t))
                    {
                        std::int8_t decoded = 0;
                        result = write ? mWriter && mWriter->writeI8(static_cast<std::int8_t>(data))
                                       : mReader && mReader->readI8(decoded);
                        if (result && !write)
                            data = static_cast<Type>(decoded);
                    }
                    else if constexpr (sizeof(Type) == sizeof(std::int16_t))
                    {
                        std::int16_t decoded = 0;
                        result = write ? mWriter && mWriter->writeI16(static_cast<std::int16_t>(data))
                                       : mReader && mReader->readI16(decoded);
                        if (result && !write)
                            data = static_cast<Type>(decoded);
                    }
                    else if constexpr (sizeof(Type) == sizeof(std::int32_t))
                    {
                        std::int32_t decoded = 0;
                        result = write ? mWriter && mWriter->writeI32(static_cast<std::int32_t>(data))
                                       : mReader && mReader->readI32(decoded);
                        if (result && !write)
                            data = static_cast<Type>(decoded);
                    }
                    else if constexpr (sizeof(Type) == sizeof(std::int64_t))
                    {
                        std::int64_t decoded = 0;
                        result = write ? mWriter && mWriter->writeI64(static_cast<std::int64_t>(data))
                                       : mReader && mReader->readI64(decoded);
                        if (result && !write)
                            data = static_cast<Type>(decoded);
                    }
                    else
                        static_assert(unsupportedPacketType<Type>, "unsupported signed packet integer width");
                }
                else
                {
                    if constexpr (sizeof(Type) == sizeof(std::uint8_t))
                    {
                        std::uint8_t decoded = 0;
                        result = write ? mWriter && mWriter->writeU8(static_cast<std::uint8_t>(data))
                                       : mReader && mReader->readU8(decoded);
                        if (result && !write)
                            data = static_cast<Type>(decoded);
                    }
                    else if constexpr (sizeof(Type) == sizeof(std::uint16_t))
                    {
                        std::uint16_t decoded = 0;
                        result = write ? mWriter && mWriter->writeU16(static_cast<std::uint16_t>(data))
                                       : mReader && mReader->readU16(decoded);
                        if (result && !write)
                            data = static_cast<Type>(decoded);
                    }
                    else if constexpr (sizeof(Type) == sizeof(std::uint32_t))
                    {
                        std::uint32_t decoded = 0;
                        result = write ? mWriter && mWriter->writeU32(static_cast<std::uint32_t>(data))
                                       : mReader && mReader->readU32(decoded);
                        if (result && !write)
                            data = static_cast<Type>(decoded);
                    }
                    else if constexpr (sizeof(Type) == sizeof(std::uint64_t))
                    {
                        std::uint64_t decoded = 0;
                        result = write ? mWriter && mWriter->writeU64(static_cast<std::uint64_t>(data))
                                       : mReader && mReader->readU64(decoded);
                        if (result && !write)
                            data = static_cast<Type>(decoded);
                    }
                    else
                        static_assert(unsupportedPacketType<Type>, "unsupported unsigned packet integer width");
                }
                if (!(write ? writeResult(result) : readResult(result)))
                    return false;
                return true;
            }
            else if constexpr (std::is_same_v<Type, float>)
            {
                float decoded = 0.f;
                const bool result = write ? mWriter && mWriter->writeFloat(data)
                                          : mReader && mReader->readFloat(decoded);
                if (!(write ? writeResult(result) : readResult(result)))
                    return false;
                if (!write)
                    data = decoded;
                return true;
            }
            else if constexpr (std::is_same_v<Type, double>)
            {
                double decoded = 0.0;
                const bool result = write ? mWriter && mWriter->writeDouble(data)
                                          : mReader && mReader->readDouble(decoded);
                if (!(write ? writeResult(result) : readResult(result)))
                    return false;
                if (!write)
                    data = decoded;
                return true;
            }
            else if constexpr (std::is_array_v<Type>)
            {
                using Element = std::remove_extent_t<Type>;
                constexpr std::size_t count = std::extent_v<Type>;
                std::array<Element, count> decoded{};
                for (std::size_t index = 0; index < count; ++index)
                {
                    Element& value = write ? data[index] : decoded[index];
                    if (!RW(value, write))
                        return false;
                }
                if (!write)
                {
                    for (std::size_t index = 0; index < count; ++index)
                        data[index] = std::move(decoded[index]);
                }
                return true;
            }
            else
                static_assert(unsupportedPacketType<Type>,
                    "packet structs must serialize each field explicitly");
        }

        template <class Value, std::size_t Count>
        bool RW(std::array<Value, Count>& value, bool write, bool compress = false)
        {
            (void)compress;
            std::array<Value, Count> decoded = value;
            auto& target = write ? value : decoded;
            for (auto& element : target)
            {
                if (!RW(element, write))
                    return false;
            }
            if (!write)
                value = std::move(decoded);
            return true;
        }

        bool RW(mwmp::transport::TransportConnectionId& value, bool write, bool compress = false);

        bool RW(ESM::Cell::DATAstruct& value, bool write, bool compress = false)
        {
            (void)compress;
            ESM::Cell::DATAstruct decoded = value;
            auto& target = write ? value : decoded;
            if (!RW(target.mFlags, write) || !RW(target.mX, write) || !RW(target.mY, write))
                return false;
            if (!write)
                value = decoded;
            return true;
        }

        bool RW(ESM::Position& value, bool write, bool compress = false)
        {
            (void)compress;
            ESM::Position decoded = value;
            auto& target = write ? value : decoded;
            if (!RW(target.pos, write) || !RW(target.rot, write))
                return false;
            if (!write)
                value = decoded;
            return true;
        }

        template <class Value>
        bool RW(ESM::StatState<Value>& value, bool write, bool compress = false)
        {
            (void)compress;
            ESM::StatState<Value> decoded = value;
            auto& target = write ? value : decoded;
            if (!RW(target.mBase, write) || !RW(target.mMod, write) || !RW(target.mCurrent, write)
                || !RW(target.mDamage, write) || !RW(target.mProgress, write))
                return false;
            if (!write)
                value = decoded;
            return true;
        }

        bool RW(ESM::FormId& value, bool write, bool compress = false)
        {
            (void)compress;
            ESM::FormId decoded = value;
            auto& target = write ? value : decoded;
            if (!RW(target.mIndex, write) || !RW(target.mContentFile, write))
                return false;
            if (!write)
                value = decoded;
            return true;
        }

        bool RW(std::variant<ESM::RefId, ESM::FormId>& value, bool write, bool compress = false)
        {
            (void)compress;
            std::uint8_t alternative = write && std::holds_alternative<ESM::FormId>(value) ? 1U : 0U;
            if (!RW(alternative, write))
                return false;
            if (alternative > 1U)
                return invalidate(protocol::CodecError::InvalidValue);

            std::variant<ESM::RefId, ESM::FormId> decoded;
            if (alternative == 0U)
            {
                ESM::RefId refId = write ? std::get<ESM::RefId>(value) : ESM::RefId{};
                if (!RW(refId, write))
                    return false;
                decoded = std::move(refId);
            }
            else
            {
                ESM::FormId formId = write ? std::get<ESM::FormId>(value) : ESM::FormId{};
                if (!RW(formId, write))
                    return false;
                decoded = formId;
            }
            if (!write)
                value = std::move(decoded);
            return true;
        }

        bool RWCount(std::uint32_t& count, bool write,
            std::uint32_t maximum = protocol::limits::defaultCollectionElements)
        {
            if (!packetValid)
                return false;

            if (write && count > maximum)
                return invalidate(protocol::CodecError::LimitExceeded);
            if (!RW(count, write))
                return false;
            if (count > maximum)
                return invalidate(protocol::CodecError::LimitExceeded);
            return true;
        }

        const static uint32_t maxStrSize = protocol::limits::defaultStringBytes;

        bool RW(std::string &str, bool write, bool compress = false, std::string::size_type maxSize = maxStrSize)
        {
            if (!packetValid)
                return false;

            if (write)
            {
                if (str.size() > maxSize || !protocol::isValidUtf8(std::as_bytes(std::span(str))))
                    return invalidate(str.size() > maxSize ? protocol::CodecError::LimitExceeded
                                                          : protocol::CodecError::InvalidUtf8);
                (void)compress;
                return writeResult(mWriter && mWriter->writeString(str, maxSize));
            }

            std::string decoded;
            if (!readResult(mReader && mReader->readString(decoded, maxSize)))
                return false;
            str = std::move(decoded);
            return true;
        }

        bool RW(ESM::RefId &refId, bool write, bool compress = false)
        {
            if (write)
            {
                std::string str = refId.getRefIdString();
                return RW(str, write, compress);
            }
            else
            {
                std::string str;
                bool res = RW(str, write, compress);
                if (res)
                    refId = ESM::RefId::stringRefId(str);
                return res;
            }
        }

        bool RW(ESM::Path& path, bool write, bool compress = false)
        {
            if (write)
            {
                std::string value = path.getOriginal();
                return RW(value, write, compress);
            }

            std::string value;
            const bool result = RW(value, write, compress);
            if (result)
                path = std::move(value);
            return result;
        }

    protected:
        bool invalidate(protocol::CodecError error)
        {
            packetValid = false;
            if (codecError == protocol::CodecError::None)
                codecError = error;
            return false;
        }

        bool writeResult(bool result);
        bool readResult(bool result);
        bool finishWrite();
        bool finishRead();
        bool prepareWrite();
        std::span<const std::byte> writePayload() const noexcept;
        uint32_t dispatchRequest(mwmp::transport::TransportConnectionId targetGuid);
        uint32_t dispatchPacket(transport::TransportConnectionId destination);
        uint32_t dispatchPacket(bool toOther);

        uint8_t packetID;
        RakNet::BitStream *bsRead, *bsSend, *bs;
        std::optional<protocol::PacketReader> mReader;
        std::optional<protocol::PacketWriter> mWriter;
        transport::ApplicationPacketDispatcher* mDispatcher = nullptr;
        mwmp::transport::TransportConnectionId guid;
        bool packetValid;
        protocol::CodecError codecError;
    };
}

#endif //OPENMW_BASEPACKET_HPP
