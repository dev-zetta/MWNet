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
                Networking* networking = Networking::getPtr();
                networking->sanitizePlayerCast(player);
                if (player.cast.pressed)
                {
                    player.sendToLoaded(&packet);
                    return;
                }

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
                std::string rejectionReason;
                if (!networking->resolvePlayerCast(player, rejectionReason))
                {
                    Script::Call<Script::CallbackIdentity(
                        "OnPlayerCastIntentRejected")>(
                            player.getId(), rejectionReason.c_str());
                    return;
                }
                player.sendToLoaded(&packet);
            }
        }
    };
}

#endif //OPENMW_PROCESSORPLAYERCAST_HPP
