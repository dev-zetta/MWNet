#ifndef OPENMW_PROCESSORACTORCAST_HPP
#define OPENMW_PROCESSORACTORCAST_HPP

#include "../ActorProcessor.hpp"
#include "apps/openmw-mp/Networking.hpp"

#include <limits>
#include <vector>

namespace mwmp
{
    class ProcessorActorCast : public ActorProcessor
    {
    public:
        ProcessorActorCast()
        {
            BPP_INIT(ID_ACTOR_CAST)
        }

        bool Validate(Player& player, const BaseActorList& incoming) override
        {
            return Networking::getPtr()->validateActorCasts(player, incoming);
        }

        void Do(ActorPacket &packet, Player &player, BaseActorList &actorList) override
        {
            // Send only to players who have the cell loaded
            Cell *serverCell = CellController::get()->getCell(&actorList.cell);

            if (serverCell == nullptr || *serverCell->getAuthority() != actorList.guid)
                return;

            Networking* networking = Networking::getPtr();
            const std::string cellDescription = actorList.cell.getShortDescription();
            std::vector<BaseActor> accepted;
            accepted.reserve(actorList.baseActors.size());
            BaseActorList deathList;
            deathList.guid = player.guid;
            deathList.cell = actorList.cell;
            deathList.authorityLeaseId = actorList.authorityLeaseId;
            for (std::size_t index = 0; index < actorList.baseActors.size(); ++index)
            {
                BaseActor& actor = actorList.baseActors[index];
                networking->sanitizeActorCast(actor);
                if (actor.cast.pressed)
                {
                    accepted.push_back(actor);
                    continue;
                }

                unsigned short targetPid = std::numeric_limits<unsigned short>::max();
                if (actor.cast.target.isPlayer)
                {
                    if (Player* target = Players::getPlayer(actor.cast.target.guid))
                        targetPid = target->getId();
                }
                const bool allowed = Script::CallBoolean<Script::CallbackIdentity(
                    "OnActorCastIntent")>(player.getId(), cellDescription.c_str(),
                    static_cast<unsigned int>(index), actor.cast.type == Cast::ITEM,
                    actor.cast.pressed, targetPid, actor.cast.target.refNum,
                    actor.cast.target.mpNum);
                if (!allowed)
                {
                    const char* reason = "denied by script";
                    Script::Call<Script::CallbackIdentity(
                        "OnActorCastIntentRejected")>(player.getId(),
                        cellDescription.c_str(), static_cast<unsigned int>(index),
                        reason);
                    continue;
                }
                std::string rejectionReason;
                std::optional<BaseActor> actorDeath;
                if (!networking->resolveActorCast(player, actorList, index,
                    actorDeath, rejectionReason))
                {
                    LOG_MESSAGE_SIMPLE(TimedLog::LOG_VERBOSE,
                        "Actor cast %u-%u source %s in %s rejected: %s",
                        actor.refNum, actor.mpNum,
                        (actor.cast.type == Cast::ITEM ? actor.cast.itemId : actor.cast.spellId).c_str(),
                        cellDescription.c_str(), rejectionReason.c_str());
                    Script::Call<Script::CallbackIdentity(
                        "OnActorCastIntentRejected")>(player.getId(),
                        cellDescription.c_str(), static_cast<unsigned int>(index),
                        rejectionReason.c_str());
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
                serverCell->sendToLoaded(&packet, &actorList);

            if (!deathList.baseActors.empty())
            {
                deathList.count = deathList.baseActors.size();
                ActorPacket* deathPacket = Networking::get()
                    .getActorPacketController()->GetPacket(ID_ACTOR_DEATH);
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

#endif //OPENMW_PROCESSORACTORCAST_HPP
