#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketChatMessage.hpp"

mwmp::PacketChatMessage::PacketChatMessage() : PlayerPacket()
{
    packetID = ID_CHAT_MESSAGE;
}

void mwmp::PacketChatMessage::Packet(bool send)
{
    PlayerPacket::Packet(send);

    Field(player->chatMessage, false, protocol::limits::chatMessageBytes);
}
