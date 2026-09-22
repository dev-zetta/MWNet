#ifndef OPENMW_PROCESSORACTORATTACK_HPP
#define OPENMW_PROCESSORACTORATTACK_HPP

#include <components/openmw-mp/Mechanics/AttackAnimation.hpp>
#include "../ActorProcessor.hpp"
#include "apps/openmw-mp/Networking.hpp"

#include <limits>
#include <vector>

namespace mwmp
{
    class ProcessorActorAttack : public ActorProcessor
    {
    public:
        ProcessorActorAttack()
        {
            BPP_INIT(ID_ACTOR_ATTACK)
        }

        bool Validate(Player& player, const BaseActorList& incoming) override
        {
            return Networking::getPtr()->validateActorAttacks(player, incoming);
        }

        void Do(ActorPacket &packet, Player &player, BaseActorList &actorList) override
        {
            Cell *serverCell = CellController::get()->getCell(&actorList.cell);
            if (serverCell == nullptr || *serverCell->getAuthority() != actorList.guid)
                return;

            Networking* networking = Networking::getPtr();
            std::vector<BaseActor> accepted;
            accepted.reserve(actorList.baseActors.size());
            const std::string cellDescription = actorList.cell.getShortDescription();
            BaseActorList deathList;
            deathList.guid = player.guid;
            deathList.cell = actorList.cell;
            deathList.authorityLeaseId = actorList.authorityLeaseId;
            for (std::size_t index = 0; index < actorList.baseActors.size(); ++index)
            {
                BaseActor& actor = actorList.baseActors[index];
                const bool animationOnly = mechanics::isAttackAnimationOnly(actor.attack);
                networking->sanitizeActorAttack(actor);
                if (animationOnly)
                {
                    accepted.push_back(actor);
                    continue;
                }

                unsigned short targetPid = std::numeric_limits<unsigned short>::max();
                if (actor.attack.target.isPlayer)
                {
                    if (Player* target = Players::getPlayer(actor.attack.target.guid))
                        targetPid = target->getId();
                }
                const bool allowed = Script::CallBoolean<Script::CallbackIdentity(
                    "OnActorAttackIntent")>(player.getId(), cellDescription.c_str(),
                    static_cast<unsigned int>(index),
                    actor.attack.type == Attack::RANGED, targetPid,
                    actor.attack.target.refNum, actor.attack.target.mpNum,
                    static_cast<double>(actor.attack.attackStrength));
                if (!allowed)
                {
                    const char* reason = "denied by script";
                    Script::Call<Script::CallbackIdentity(
                        "OnActorAttackIntentRejected")>(player.getId(), cellDescription.c_str(),
                        static_cast<unsigned int>(index), reason);
                    continue;
                }

                std::string rejectionReason;
                std::optional<BaseActor> actorDeath;
                if (!networking->resolveActorAttack(player, actorList, index,
                    actorDeath, rejectionReason))
                {
                    LOG_MESSAGE_SIMPLE(TimedLog::LOG_VERBOSE,
                        "Actor attack %u-%u in %s rejected: %s", actor.refNum, actor.mpNum,
                        cellDescription.c_str(), rejectionReason.c_str());
                    Script::Call<Script::CallbackIdentity(
                        "OnActorAttackIntentRejected")>(player.getId(), cellDescription.c_str(),
                        static_cast<unsigned int>(index), rejectionReason.c_str());
                    continue;
                }
                accepted.push_back(actorList.baseActors[index]);
                if (actorDeath)
                {
                    actorDeath->killer.isPlayer = false;
                    actorDeath->killer.refId = actor.refId;
                    actorDeath->killer.refNum = actor.refNum;
                    actorDeath->killer.mpNum = actor.mpNum;
                    deathList.baseActors.push_back(std::move(*actorDeath));
                }
            }

            actorList.baseActors = std::move(accepted);
            actorList.count = actorList.baseActors.size();
            if (!actorList.baseActors.empty())
            {
                BaseActorList ownResults = actorList;
                std::erase_if(ownResults.baseActors, [](const BaseActor& actor) {
                    return !actor.attack.unarmed;
                });
                ownResults.count = ownResults.baseActors.size();
                if (!ownResults.baseActors.empty())
                {
                    packet.setActorList(&ownResults);
                    packet.Send(player.guid);
                    packet.setActorList(&actorList);
                }
                serverCell->sendToLoaded(&packet, &actorList);
            }

            if (!deathList.baseActors.empty())
            {
                deathList.count = deathList.baseActors.size();
                ActorPacket* deathPacket = Networking::get().getActorPacketController()
                    ->GetPacket(ID_ACTOR_DEATH);
                deathPacket->setActorList(&deathList);
                deathPacket->Send(player.guid);
                serverCell->sendToLoaded(deathPacket, &deathList);

                actorList = deathList;
                Script::Call<Script::CallbackIdentity("OnActorDeath")>(
                    player.getId(), cellDescription.c_str());
            }
        }
    };
}

#endif //OPENMW_PROCESSORACTORATTACK_HPP
