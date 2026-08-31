#ifndef OPENMW_PACKETCHATMESSAGE_HPP
#define OPENMW_PACKETCHATMESSAGE_HPP

#include <components/openmw-mp/Packets/Player/PlayerPacket.hpp>

namespace mwmp
{
    class PacketChatMessage : public PlayerPacket
    {
    public:
        PacketChatMessage();

        virtual void Packet(RakNet::BitStream *newBitstream, bool send);
    };
}

#endif //OPENMW_PACKETCHATMESSAGE_HPP
