#include <components/openmw-mp/NetworkMessages.hpp>
#include <PacketPriority.h>
#include <RakPeer.h>
#include "BasePacket.hpp"

using namespace mwmp;

BasePacket::BasePacket(RakNet::RakPeerInterface *peer)
    : packetID(0)
    , reliability(RELIABLE_ORDERED)
    , priority(HIGH_PRIORITY)
    , orderChannel(CHANNEL_SYSTEM)
    , bsRead(nullptr)
    , bsSend(nullptr)
    , bs(nullptr)
    , peer(peer)
    , guid(RakNet::UNASSIGNED_CRABNET_GUID)
    , packetValid(false)
    , codecError(protocol::CodecError::None)
{
}

void BasePacket::Packet(RakNet::BitStream *newBitstream, bool send)
{
    bs = newBitstream;
    packetValid = true;
    codecError = protocol::CodecError::None;

    if (bs == nullptr)
    {
        invalidate(protocol::CodecError::InvalidValue);
        return;
    }

    mReader.reset();
    mWriter.reset();
    if (send)
    {
        mWriter.emplace(protocol::limits::normalMessageBytes);
        return;
    }

    if (bs->GetReadOffset() % 8U != 0 || bs->GetNumberOfUnreadBits() % 8U != 0)
    {
        invalidate(protocol::CodecError::InvalidValue);
        return;
    }
    const std::size_t offset = bs->GetReadOffset() / 8U;
    const std::size_t size = bs->GetNumberOfUnreadBits() / 8U;
    mReader.emplace(std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(bs->GetData() + offset), size));
}

void BasePacket::SetReadStream(RakNet::BitStream *bitStream)
{
    bsRead = bitStream;
}

void BasePacket::SetSendStream(RakNet::BitStream *bitStream)
{
    bsSend = bitStream;
}

void BasePacket::SetStreams(RakNet::BitStream *inStream, RakNet::BitStream *outStream)
{
    if (inStream != nullptr)
        bsRead = inStream;
    if (outStream != nullptr)
        bsSend = outStream;
}

uint32_t BasePacket::RequestData(RakNet::RakNetGUID targetGuid)
{
    if (bsSend == nullptr || peer == nullptr)
        return 0;

    bsSend->ResetWritePointer();
    bsSend->Write(packetID);
    bsSend->Write(targetGuid.g);
    return peer->Send(bsSend, HIGH_PRIORITY, RELIABLE_ORDERED, orderChannel, targetGuid, false);
}

uint32_t BasePacket::Send(RakNet::AddressOrGUID destination)
{
    if (bsSend == nullptr || peer == nullptr)
        return 0;

    bsSend->ResetWritePointer();
    Packet(bsSend, true);
    if (!finishWrite())
        return 0;
    return peer->Send(bsSend, priority, reliability, orderChannel, destination, false);
}

uint32_t BasePacket::Send(bool toOther)
{
    if (bsSend == nullptr || peer == nullptr)
        return 0;

    bsSend->ResetWritePointer();
    Packet(bsSend, true);
    if (!finishWrite())
        return 0;
    return peer->Send(bsSend, priority, reliability, orderChannel, guid, toOther);
}

void BasePacket::Read()
{
    if (bsRead == nullptr)
    {
        invalidate(protocol::CodecError::InvalidValue);
        return;
    }
    Packet(bsRead, false);
    finishRead();
}

bool BasePacket::RW(RakNet::RakNetGUID& value, bool write, bool compress)
{
    (void)compress;
    std::uint64_t decoded = value.g;
    if (!RW(decoded, write))
        return false;
    if (!write)
        value = RakNet::RakNetGUID(decoded);
    return true;
}

bool BasePacket::writeResult(bool result)
{
    if (result)
        return true;
    return invalidate(mWriter ? mWriter->error() : protocol::CodecError::InvalidValue);
}

bool BasePacket::readResult(bool result)
{
    if (result)
        return true;
    return invalidate(mReader ? mReader->error() : protocol::CodecError::InvalidValue);
}

bool BasePacket::finishWrite()
{
    if (!packetValid || !mWriter || !mWriter->valid() || bsSend == nullptr)
        return false;
    bsSend->Write(packetID);
    bsSend->Write(guid.g);
    const auto payload = mWriter->bytes();
    if (!payload.empty())
        bsSend->Write(reinterpret_cast<const char*>(payload.data()), payload.size());
    if (bsSend->GetNumberOfBytesUsed() > protocol::limits::normalMessageBytes + headerSize())
        return invalidate(protocol::CodecError::LimitExceeded);
    return true;
}

bool BasePacket::finishRead()
{
    if (!packetValid || !mReader)
        return false;
    return readResult(mReader->finish());
}

void BasePacket::setGUID(RakNet::RakNetGUID newGuid)
{
    guid = newGuid;
}

RakNet::RakNetGUID BasePacket::getGUID()
{
    return guid;
}
