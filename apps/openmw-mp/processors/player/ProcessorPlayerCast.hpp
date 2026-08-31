#ifndef OPENMW_PROCESSORPLAYERCAST_HPP
#define OPENMW_PROCESSORPLAYERCAST_HPP

#include "../PlayerProcessor.hpp"
#include "apps/openmw-mp/Networking.hpp"

#include <limits>

namespace mwmp
{
    class ProcessorPlayerCast : public PlayerProcessor
    {
        PlayerPacketController *playerController;
    public:
        ProcessorPlayerCast()
        {
            BPP_INIT(ID_PLAYER_CAST)
        }

        bool Validate(Player& player, const BasePlayer& incoming) override
        {
            return Networking::getPtr()->validatePlayerCast(player, incoming);
        }

        void Do(PlayerPacket &packet, Player &player) override
        {
            DEBUG_PRINTF(strPacketID.c_str());

            if (!player.creatureStats.mDead)
            {
                unsigned short targetPid = std::numeric_limits<unsigned short>::max();
                if (player.cast.target.isPlayer)
                {
                    if (Player* target = Players::getPlayer(player.cast.target.guid))
                        targetPid = target->getId();
                }
                const bool allowed = Script::CallBoolean<Script::CallbackIdentity(
                    "OnPlayerCastIntent")>(player.getId(),
                    player.cast.type == Cast::ITEM, player.cast.pressed, targetPid,
                    player.cast.target.refNum, player.cast.target.mpNum);
                if (!allowed)
                {
                    const char* reason = "denied by script";
                    Script::Call<Script::CallbackIdentity(
                        "OnPlayerCastIntentRejected")>(player.getId(), reason);
                    return;
                }
                player.sendToLoaded(&packet);
            }
        }
    };
}

#endif //OPENMW_PROCESSORPLAYERCAST_HPP
