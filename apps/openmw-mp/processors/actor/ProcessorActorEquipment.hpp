#ifndef OPENMW_PROCESSORACTOREQUIPMENT_HPP
#define OPENMW_PROCESSORACTOREQUIPMENT_HPP

#include "../ActorProcessor.hpp"
#include "apps/openmw-mp/Networking.hpp"

namespace mwmp
{
    class ProcessorActorEquipment : public ActorProcessor
    {
    public:
        ProcessorActorEquipment()
        {
            BPP_INIT(ID_ACTOR_EQUIPMENT)
        }

        bool Validate(Player& player, const BaseActorList& incoming) override
        {
            return Networking::getPtr()->validateActorEquipment(player, incoming);
        }

        void Do(ActorPacket &packet, Player &player, BaseActorList &actorList) override
        {
            // Send only to players who have the cell loaded
            Cell *serverCell = CellController::get()->getCell(&actorList.cell);

            if (serverCell != nullptr)
            {
                const std::string cellDescription
                    = actorList.cell.getShortDescription();
                const bool allowed = Script::CallBoolean<
                    Script::CallbackIdentity("OnActorEquipmentIntent")>(
                        player.getId(), cellDescription.c_str());
                if (!allowed)
                    return;

                if (!Networking::getPtr()->commitActorEquipment(player, actorList))
                {
                    Script::Call<Script::CallbackIdentity(
                        "OnActorEquipmentIntentRejected")>(
                            player.getId(), cellDescription.c_str());
                    return;
                }

                Script::Call<Script::CallbackIdentity("OnActorEquipment")>(player.getId(), actorList.cell.getShortDescription().c_str());

                serverCell->sendToLoaded(&packet, &actorList);
            }
        }
    };
}

#endif //OPENMW_PROCESSORACTOREQUIPMENT_HPP
