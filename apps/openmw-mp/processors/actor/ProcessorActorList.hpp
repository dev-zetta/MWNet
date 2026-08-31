#ifndef OPENMW_PROCESSORACTORLIST_HPP
#define OPENMW_PROCESSORACTORLIST_HPP

#include "../ActorProcessor.hpp"

namespace mwmp
{
    class ProcessorActorList : public ActorProcessor
    {
    public:
        ProcessorActorList()
        {
            BPP_INIT(ID_ACTOR_LIST)
        }

        bool Validate(Player& player, const BaseActorList& actorList) override
        {
            return Networking::getPtr()->validateActorList(player, actorList);
        }

        void Do(ActorPacket &packet, Player &player, BaseActorList &actorList) override
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Received %s from %s", strPacketID.c_str(), player.npc.mName.c_str());
            
            // Send only to players who have the cell loaded
            Cell *serverCell = CellController::get()->getCell(&actorList.cell);

            if (serverCell == nullptr)
                return;

            const std::string cellDescription = actorList.cell.getShortDescription();
            const bool allowed = Script::CallBoolean<
                Script::CallbackIdentity("OnActorListIntent")>(
                    player.getId(), cellDescription.c_str());
            if (!allowed)
                return;

            if (!Networking::getPtr()->commitActorList(player, actorList))
            {
                Script::Call<Script::CallbackIdentity("OnActorListIntentRejected")>(
                    player.getId(), cellDescription.c_str());
                return;
            }

            Script::Call<Script::CallbackIdentity("OnActorList")>(
                player.getId(), cellDescription.c_str());
            serverCell->sendToLoaded(&packet, &actorList);
        }
    };
}

#endif //OPENMW_PROCESSORACTORLIST_HPP
