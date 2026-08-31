#ifndef OPENMW_PROCESSORACTORCELLCHANGE_HPP
#define OPENMW_PROCESSORACTORCELLCHANGE_HPP

#include "../ActorProcessor.hpp"
#include "apps/openmw-mp/Networking.hpp"

namespace mwmp
{
    class ProcessorActorCellChange : public ActorProcessor
    {
    public:
        ProcessorActorCellChange()
        {
            BPP_INIT(ID_ACTOR_CELL_CHANGE)
        }

        bool Validate(Player& player, const BaseActorList& incoming) override
        {
            return Networking::getPtr()->validateActorCellChanges(player, incoming);
        }

        void Do(ActorPacket &packet, Player &player, BaseActorList &actorList) override
        {
            const std::string sourceCell = actorList.cell.getShortDescription();
            const bool allowed = Script::CallBoolean<
                Script::CallbackIdentity("OnActorCellChangeIntent")>(
                    player.getId(), sourceCell.c_str());
            if (!allowed)
            {
                const char* reason = "denied by script";
                Script::Call<Script::CallbackIdentity(
                    "OnActorCellChangeIntentRejected")>(player.getId(),
                        sourceCell.c_str(), reason);
                return;
            }

            if (!Networking::getPtr()->commitActorCellChanges(player, actorList))
            {
                const char* reason = "canonical cell transition rejected";
                Script::Call<Script::CallbackIdentity(
                    "OnActorCellChangeIntentRejected")>(player.getId(),
                        sourceCell.c_str(), reason);
                return;
            }

            Script::Call<Script::CallbackIdentity("OnActorCellChange")>(
                player.getId(), sourceCell.c_str());

            // Cell changes are relevant to visitors of the source and destination.
            // The packet layer de-duplicates recipients while retaining legacy relay
            // behavior for scripts that observe the post-commit callback.
            packet.Send(true);
        }
    };
}

#endif //OPENMW_PROCESSORACTORCELLCHANGE_HPP
