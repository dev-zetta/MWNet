#ifndef OPENMW_PLAYERPACKET_HPP
#define OPENMW_PLAYERPACKET_HPP

#include <string>
#include <BitStream.h>
#include <components/openmw-mp/Base/BasePlayer.hpp>

#include <components/openmw-mp/Packets/BasePacket.hpp>

namespace mwmp
{
    class PlayerPacket : public BasePacket
    {
    public:
        PlayerPacket();

        ~PlayerPacket();

        void setPlayer(BasePlayer *newPlayer);
        BasePlayer *getPlayer();

    protected:
        BasePlayer *player = nullptr;

    };
}

#endif //OPENMW_PLAYERPACKET_HPP
