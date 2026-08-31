#ifndef OPENMW_BASEPLAYERPROCESSOR_HPP
#define OPENMW_BASEPLAYERPROCESSOR_HPP

#include <components/openmw-mp/Base/BasePacketProcessor.hpp>
#include <components/openmw-mp/Packets/BasePacket.hpp>
#include <components/openmw-mp/NetworkMessages.hpp>
#include "Player.hpp"

namespace mwmp
{
    class PlayerProcessor : public BasePacketProcessor<PlayerProcessor>
    {
    public:
        virtual ~PlayerProcessor() = default;

        virtual void Do(PlayerPacket &packet, Player &player) = 0;
        virtual bool Validate(Player&, const BasePlayer&) { return true; }

        static bool Process(RakNet::Packet &packet);
    };
}

#endif //OPENMW_BASEPLAYERPROCESSOR_HPP
