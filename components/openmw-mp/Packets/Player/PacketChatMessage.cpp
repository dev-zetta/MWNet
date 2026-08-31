#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketChatMessage.hpp"

mwmp::PacketChatMessage::PacketChatMessage() : PlayerPacket()
{
    packetID = ID_CHAT_MESSAGE;
    orderChannel = CHANNEL_SYSTEM;
}

void mwmp::PacketChatMessage::Packet(RakNet::BitStream *newBitstream, bool send)
{
    PlayerPacket::Packet(newBitstream, send);

    RW(player->chatMessage, send, false, protocol::limits::chatMessageBytes);
}
