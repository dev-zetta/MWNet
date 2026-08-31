#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/Protocol/ApplicationPacketId.hpp>
#include <components/openmw-mp/Transport/ApplicationPacketDispatcher.hpp>
#include "BasePacket.hpp"

using namespace mwmp;

BasePacket::BasePacket()
    : packetID(0)
    , bsRead(nullptr)
    , bsSend(nullptr)
    , bs(nullptr)
    , guid(mwmp::transport::TransportConnectionId{})
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

uint32_t BasePacket::RequestData(mwmp::transport::TransportConnectionId targetGuid)
{
    return dispatchRequest(targetGuid);
}

uint32_t BasePacket::Send(transport::TransportConnectionId destination)
{
    return dispatchPacket(destination);
}

uint32_t BasePacket::Send(bool toOther)
{
    return dispatchPacket(toOther);
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

bool BasePacket::RW(mwmp::transport::TransportConnectionId& value, bool write, bool compress)
{
    (void)compress;
    std::uint64_t decoded = value.value;
    if (!RW(decoded, write))
        return false;
    if (!write)
        value = mwmp::transport::TransportConnectionId(decoded);
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
    bsSend->Write(guid.value);
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

uint32_t BasePacket::dispatchRequest(mwmp::transport::TransportConnectionId targetGuid)
{
    if (mDispatcher == nullptr || !protocol::isApplicationPacketId(packetID))
        return 0;

    transport::TransportError error;
    const auto id = static_cast<protocol::ApplicationPacketId>(packetID);
    bool sent = false;
    if (mDispatcher->flow() == transport::ApplicationPacketFlow::ClientToServer)
        sent = mDispatcher->sendToServer(id, targetGuid.value, {}, error);
    else
        sent = mDispatcher->sendTo(id, targetGuid.value,
            transport::TransportConnectionId{ targetGuid.value }, {}, error);
    return sent ? 1U : 0U;
}

uint32_t BasePacket::dispatchPacket(transport::TransportConnectionId destination)
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
        sent = mDispatcher->sendToServer(id, guid.value, writePayload(), error);
    else if (destination)
        sent = mDispatcher->sendTo(id, guid.value, destination, writePayload(), error);
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
        sent = mDispatcher->sendToServer(id, guid.value, writePayload(), error);
    else if (toOther)
        sent = mDispatcher->sendToAll(id, guid.value, writePayload(), error,
            transport::TransportConnectionId{ guid.value });
    else
        sent = mDispatcher->sendTo(id, guid.value,
            transport::TransportConnectionId{ guid.value }, writePayload(), error);
    return sent ? 1U : 0U;
}

bool BasePacket::finishRead()
{
    if (!packetValid || !mReader)
        return false;
    return readResult(mReader->finish());
}

void BasePacket::setGUID(mwmp::transport::TransportConnectionId newGuid)
{
    guid = newGuid;
}

mwmp::transport::TransportConnectionId BasePacket::getGUID()
{
    return guid;
}
