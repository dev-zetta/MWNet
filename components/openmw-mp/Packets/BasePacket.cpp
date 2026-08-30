#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/Protocol/ApplicationPacketId.hpp>
#include <components/openmw-mp/Transport/ApplicationPacketDispatcher.hpp>
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

void BasePacket::SetApplicationPacketDispatcher(
    transport::ApplicationPacketDispatcher* dispatcher)
{
    mDispatcher = dispatcher;
}

uint32_t BasePacket::RequestData(RakNet::RakNetGUID targetGuid)
{
    if (mDispatcher != nullptr)
        return dispatchRequest(targetGuid);
    if (bsSend == nullptr || peer == nullptr)
        return 0;

    bsSend->ResetWritePointer();
    bsSend->Write(packetID);
    bsSend->Write(targetGuid.g);
    return peer->Send(bsSend, HIGH_PRIORITY, RELIABLE_ORDERED, orderChannel, targetGuid, false);
}

uint32_t BasePacket::Send(RakNet::AddressOrGUID destination)
{
    if (mDispatcher != nullptr)
        return dispatchPacket(destination);
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
    if (mDispatcher != nullptr)
        return dispatchPacket(toOther);
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
    if (!prepareWrite() || bsSend == nullptr)
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

bool BasePacket::prepareWrite()
{
    if (!packetValid || !mWriter || !mWriter->valid())
        return false;
    if (mWriter->bytes().size() > protocol::limits::normalMessageBytes)
        return invalidate(protocol::CodecError::LimitExceeded);
    return true;
}

std::span<const std::byte> BasePacket::writePayload() const noexcept
{
    return mWriter ? mWriter->bytes() : std::span<const std::byte>{};
}

uint32_t BasePacket::dispatchRequest(RakNet::RakNetGUID targetGuid)
{
    if (mDispatcher == nullptr || !protocol::isApplicationPacketId(packetID))
        return 0;

    transport::TransportError error;
    const auto id = static_cast<protocol::ApplicationPacketId>(packetID);
    bool sent = false;
    if (mDispatcher->flow() == transport::ApplicationPacketFlow::ClientToServer)
        sent = mDispatcher->sendToServer(id, targetGuid.g, {}, error);
    else
        sent = mDispatcher->sendTo(id, targetGuid.g,
            transport::TransportConnectionId{ targetGuid.g }, {}, error);
    return sent ? 1U : 0U;
}

uint32_t BasePacket::dispatchPacket(RakNet::AddressOrGUID destination)
{
    if (mDispatcher == nullptr || bsSend == nullptr
        || !protocol::isApplicationPacketId(packetID))
        return 0;

    bsSend->ResetWritePointer();
    Packet(bsSend, true);
    if (!prepareWrite())
        return 0;

    transport::TransportError error;
    const auto id = static_cast<protocol::ApplicationPacketId>(packetID);
    bool sent = false;
    if (mDispatcher->flow() == transport::ApplicationPacketFlow::ClientToServer)
        sent = mDispatcher->sendToServer(id, guid.g, writePayload(), error);
    else if (destination.rakNetGuid != RakNet::UNASSIGNED_CRABNET_GUID)
        sent = mDispatcher->sendTo(id, guid.g,
            transport::TransportConnectionId{ destination.rakNetGuid.g }, writePayload(), error);
    return sent ? 1U : 0U;
}

uint32_t BasePacket::dispatchPacket(bool toOther)
{
    if (mDispatcher == nullptr || bsSend == nullptr
        || !protocol::isApplicationPacketId(packetID))
        return 0;

    bsSend->ResetWritePointer();
    Packet(bsSend, true);
    if (!prepareWrite())
        return 0;

    transport::TransportError error;
    const auto id = static_cast<protocol::ApplicationPacketId>(packetID);
    bool sent = false;
    if (mDispatcher->flow() == transport::ApplicationPacketFlow::ClientToServer)
        sent = mDispatcher->sendToServer(id, guid.g, writePayload(), error);
    else if (toOther)
        sent = mDispatcher->sendToAll(id, guid.g, writePayload(), error,
            transport::TransportConnectionId{ guid.g });
    else
        sent = mDispatcher->sendTo(id, guid.g,
            transport::TransportConnectionId{ guid.g }, writePayload(), error);
    return sent ? 1U : 0U;
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
