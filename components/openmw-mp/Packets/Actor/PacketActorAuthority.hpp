#ifndef OPENMW_PACKETACTORAUTHORITY_HPP
#define OPENMW_PACKETACTORAUTHORITY_HPP

#include <components/openmw-mp/Packets/Actor/ActorPacket.hpp>

namespace mwmp
{
    class PacketActorAuthority : public ActorPacket
    {
    public:
        PacketActorAuthority();

        virtual void Packet(RakNet::BitStream *newBitstream, bool send);
    };
}

#endif //OPENMW_PACKETACTORAUTHORITY_HPP
