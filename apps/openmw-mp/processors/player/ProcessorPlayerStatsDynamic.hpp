#ifndef OPENMW_PROCESSORPLAYERSTATS_DYNAMIC_HPP
#define OPENMW_PROCESSORPLAYERSTATS_DYNAMIC_HPP

#include "../PlayerProcessor.hpp"
#include "apps/openmw-mp/Networking.hpp"

namespace mwmp
{
    class ProcessorPlayerStatsDynamic : public PlayerProcessor
    {
    public:
        ProcessorPlayerStatsDynamic()
        {
            BPP_INIT(ID_PLAYER_STATS_DYNAMIC)
        }

        bool Validate(Player& player, const BasePlayer& incoming) override
        {
            return Networking::getPtr()->validatePlayerStats(player, incoming);
        }

        void Do(PlayerPacket &packet, Player &player) override
        {
            DEBUG_PRINTF(strPacketID.c_str());

            if (!Networking::getPtr()->reconcilePlayerStats(player))
                return;

            packet.Send(player.guid);
            player.sendToLoaded(&packet);
        }
    };
}

#endif //OPENMW_PROCESSORPLAYERSTATS_DYNAMIC_HPP
