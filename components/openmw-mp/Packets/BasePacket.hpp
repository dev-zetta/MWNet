#ifndef OPENMW_BASEPACKET_HPP
#define OPENMW_BASEPACKET_HPP

#include <algorithm>
#include <string>
#include <type_traits>
#include <utility>
#include <RakNetTypes.h>
#include <BitStream.h>
#include <PacketPriority.h>
#include <components/esm/path.hpp>
#include <components/esm/refid.hpp>
#include <components/openmw-mp/Protocol/PacketCodec.hpp>


namespace mwmp
{
    class BasePacket
    {
    public:
        explicit BasePacket(RakNet::RakPeerInterface *peer);

        virtual ~BasePacket() = default;

        virtual void Packet(RakNet::BitStream *newBitstream, bool send);
        virtual uint32_t Send(bool toOtherPlayers = true);
        virtual uint32_t Send(RakNet::AddressOrGUID destination);
        virtual void Read();

        void setGUID(RakNet::RakNetGUID newGuid);
        RakNet::RakNetGUID getGUID();

        void SetReadStream(RakNet::BitStream *bitStream);
        void SetSendStream(RakNet::BitStream *bitStream);
        void SetStreams(RakNet::BitStream *inStream, RakNet::BitStream *outStream);
        virtual uint32_t RequestData(RakNet::RakNetGUID targetGuid);

        static inline uint32_t headerSize()
        {
            return static_cast<uint32_t>(1 + RakNet::RakNetGUID::size()); // packetID + RakNetGUID (uint64_t)
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
        template<class templateType>
        bool RW(templateType &data, uint32_t size, bool write)
        {
            if (!packetValid || bs == nullptr)
                return false;

            if (write)
            {
                bs->Write(data, size);
                return true;
            }

            if (!bs->Read(data, size))
                return invalidate(protocol::CodecError::Truncated);
            return true;
        }

        template<class templateType>
        bool RW(templateType &data, bool write, bool compress = 0)
        {
            if (!packetValid || bs == nullptr)
                return false;

            if (write)
            {
                if (compress)
                    bs->WriteCompressed(data);
                else
                    bs->Write(data);
                return true;
            }
            else
            {
                templateType decoded{};
                bool result = false;
                if (compress)
                    result = bs->ReadCompressed(decoded);
                else
                    result = bs->Read(decoded);
                if (!result)
                    return invalidate(protocol::CodecError::Truncated);
                if constexpr (std::is_array_v<templateType>)
                    std::copy_n(decoded, std::extent_v<templateType>, data);
                else
                    data = std::move(decoded);
                return true;
            }
        }

        bool RW(bool &data, bool write)
        {
            if (!packetValid || bs == nullptr)
                return false;

            if (write)
            {
                bs->Write(data);
                return true;
            }

            bool decoded = false;
            if (!bs->Read(decoded))
                return invalidate(protocol::CodecError::Truncated);
            data = decoded;
            return true;
        }

        bool RWCount(std::uint32_t& count, bool write,
            std::uint32_t maximum = protocol::limits::defaultCollectionElements)
        {
            if (!packetValid || bs == nullptr)
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
            if (!packetValid || bs == nullptr)
                return false;

            if (write)
            {
                if (str.size() > maxSize || !protocol::isValidUtf8(std::as_bytes(std::span(str))))
                    return invalidate(str.size() > maxSize ? protocol::CodecError::LimitExceeded
                                                          : protocol::CodecError::InvalidUtf8);
                if (compress)
                    RakNet::RakString::SerializeCompressed(str.c_str(), bs);
                else
                {
                    RakNet::RakString rstr;
                    rstr.AppendBytes(str.c_str(), str.size());
                    bs->Write(rstr);
                }
                return true;
            }

            RakNet::RakString rstr;
            const bool result = compress ? rstr.DeserializeCompressed(bs) : bs->Read(rstr);
            if (!result)
                return invalidate(protocol::CodecError::Truncated);
            if (rstr.GetLength() > maxSize)
                return invalidate(protocol::CodecError::LimitExceeded);

            const auto encoded = std::as_bytes(std::span(rstr.C_String(), rstr.GetLength()));
            if (!protocol::isValidUtf8(encoded))
                return invalidate(protocol::CodecError::InvalidUtf8);

            str.assign(rstr.C_String(), rstr.GetLength());
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

        uint8_t packetID;
        PacketReliability reliability;
        PacketPriority priority;
        int8_t orderChannel;
        RakNet::BitStream *bsRead, *bsSend, *bs;
        RakNet::RakPeerInterface *peer;
        RakNet::RakNetGUID guid;
        bool packetValid;
        protocol::CodecError codecError;
    };
}

#endif //OPENMW_BASEPACKET_HPP
