#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/Protocol/ApplicationPacketId.hpp>
#include <components/openmw-mp/Transport/ApplicationPacketDispatcher.hpp>
#include "BasePacket.hpp"

#include <new>

using namespace mwmp;

BasePacket::BasePacket()
    : packetID(0)
    , guid(mwmp::transport::TransportConnectionId{})
    , packetValid(false)
    , codecError(protocol::CodecError::None)
{
}

void BasePacket::Packet(bool send)
{
    if (send)
    {
        packetValid = true;
        codecError = protocol::CodecError::None;
        mReader.reset();
        mWriter.reset();
        mWriter.emplace(protocol::limits::normalMessageBytes);
        return;
    }

    if (!mReader)
        invalidate(protocol::CodecError::InvalidValue);
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

void BasePacket::Read(std::span<const std::byte> payload)
{
    packetValid = true;
    codecError = protocol::CodecError::None;
    mWriter.reset();
    mReader.emplace(payload);
    try
    {
        if (!beginDecodeTransaction())
        {
            invalidate(protocol::CodecError::InvalidValue);
            rollbackDecodeTransaction();
            return;
        }

        Packet(false);
        if (finishRead())
            commitDecodeTransaction();
        else
            rollbackDecodeTransaction();
    }
    catch (const std::bad_alloc&)
    {
        invalidate(protocol::CodecError::AllocationFailed);
        rollbackDecodeTransaction();
    }
    catch (...)
    {
        invalidate(protocol::CodecError::InvalidValue);
        rollbackDecodeTransaction();
    }
}

bool BasePacket::beginDecodeTransaction()
{
    return true;
}

void BasePacket::commitDecodeTransaction() noexcept
{
}

void BasePacket::rollbackDecodeTransaction() noexcept
{
}

bool BasePacket::Field(mwmp::transport::TransportConnectionId& value, bool compress)
{
    (void)compress;
    const bool write = isWriting();
    std::uint64_t decoded = value.value;
    if (!Field(decoded))
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
    if (mDispatcher == nullptr || !protocol::isApplicationPacketId(packetID))
        return 0;

    Packet(true);
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
    if (mDispatcher == nullptr || !protocol::isApplicationPacketId(packetID))
        return 0;

    Packet(true);
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

std::size_t BasePacket::unreadPayloadBytes() const noexcept
{
    return mReader ? mReader->remaining() : 0U;
}

void BasePacket::setGUID(mwmp::transport::TransportConnectionId newGuid)
{
    guid = newGuid;
}

mwmp::transport::TransportConnectionId BasePacket::getGUID()
{
    return guid;
}
