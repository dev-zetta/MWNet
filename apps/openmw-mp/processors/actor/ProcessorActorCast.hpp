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

            const std::string cellDescription = actorList.cell.getShortDescription();
            std::vector<BaseActor> accepted;
            accepted.reserve(actorList.baseActors.size());
            for (std::size_t index = 0; index < actorList.baseActors.size(); ++index)
            {
                BaseActor& actor = actorList.baseActors[index];
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
                accepted.push_back(std::move(actor));
            }

            actorList.baseActors = std::move(accepted);
            actorList.count = actorList.baseActors.size();
            if (!actorList.baseActors.empty())
                serverCell->sendToLoaded(&packet, &actorList);
        }
    };
}

#endif //OPENMW_PROCESSORACTORCAST_HPP
