#ifndef OPENMW_PROCESSORPLAYERATTACK_HPP
#define OPENMW_PROCESSORPLAYERATTACK_HPP

#include <components/openmw-mp/Mechanics/AttackAnimation.hpp>
#include "../PlayerProcessor.hpp"
#include "apps/openmw-mp/Networking.hpp"

#include <limits>

namespace mwmp
{
    class ProcessorPlayerAttack : public PlayerProcessor
    {
        PlayerPacketController *playerController;
    public:
        ProcessorPlayerAttack()
        {
            BPP_INIT(ID_PLAYER_ATTACK)
        }

        bool Validate(Player& player, const BasePlayer& incoming) override
        {
            return Networking::getPtr()->validatePlayerAttack(player, incoming);
        }

        void Do(PlayerPacket &packet, Player &player) override
        {
            DEBUG_PRINTF(strPacketID.c_str());

            if (!player.creatureStats.mDead)
            {
                Networking* networking = Networking::getPtr();
                const bool animationOnly = mechanics::isAttackAnimationOnly(player.attack);
                networking->sanitizePlayerAttack(player);
                if (animationOnly)
                {
                    player.sendToLoaded(&packet);
                    return;
                }

                unsigned short targetPid = std::numeric_limits<unsigned short>::max();
                if (player.attack.target.isPlayer)
                {
                    if (Player* target = Players::getPlayer(player.attack.target.guid))
                        targetPid = target->getId();
                }
                const bool allowed = Script::CallBoolean<Script::CallbackIdentity(
                    "OnPlayerAttackIntent")>(player.getId(),
                    player.attack.type == Attack::RANGED, targetPid,
                    player.attack.target.refNum, player.attack.target.mpNum,
                    static_cast<double>(player.attack.attackStrength));
                if (!allowed)
                {
                    const char* rejectionReason = "denied by script";
                    Script::Call<Script::CallbackIdentity(
                        "OnPlayerAttackIntentRejected")>(player.getId(), rejectionReason);
                    return;
                }

                std::string rejectionReason;
                if (!networking->resolvePlayerAttack(player, rejectionReason))
                {
                    Script::Call<Script::CallbackIdentity(
                        "OnPlayerAttackIntentRejected")>(
                            player.getId(), rejectionReason.c_str());
                    return;
                }
                player.sendToLoaded(&packet);
            }
        }
    };
}

#endif //OPENMW_PROCESSORPLAYERATTACK_HPP
