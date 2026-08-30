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

    if (send)
    {
        bs->Write(packetID);
        bs->Write(guid);
    }
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
    bsSend->Write(targetGuid);
    return peer->Send(bsSend, HIGH_PRIORITY, RELIABLE_ORDERED, orderChannel, targetGuid, false);
}

uint32_t BasePacket::Send(RakNet::AddressOrGUID destination)
{
    if (bsSend == nullptr || peer == nullptr)
        return 0;

    bsSend->ResetWritePointer();
    Packet(bsSend, true);
    if (!packetValid || bsSend->GetNumberOfBytesUsed() > protocol::limits::normalMessageBytes + headerSize())
        return 0;
    return peer->Send(bsSend, priority, reliability, orderChannel, destination, false);
}

uint32_t BasePacket::Send(bool toOther)
{
    if (bsSend == nullptr || peer == nullptr)
        return 0;

    bsSend->ResetWritePointer();
    Packet(bsSend, true);
    if (!packetValid || bsSend->GetNumberOfBytesUsed() > protocol::limits::normalMessageBytes + headerSize())
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
}

void BasePacket::setGUID(RakNet::RakNetGUID newGuid)
{
    guid = newGuid;
}

RakNet::RakNetGUID BasePacket::getGUID()
{
    return guid;
}
