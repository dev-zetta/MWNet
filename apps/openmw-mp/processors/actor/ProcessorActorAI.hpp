#ifndef OPENMW_PROCESSORACTORAI_HPP
#define OPENMW_PROCESSORACTORAI_HPP

#include "../ActorProcessor.hpp"
#include "apps/openmw-mp/Networking.hpp"

namespace mwmp
{
    class ProcessorActorAI : public ActorProcessor
    {
    public:
        ProcessorActorAI()
        {
            BPP_INIT(ID_ACTOR_AI)
        }

        bool Validate(Player& player, const BaseActorList& incoming) override
        {
            return Networking::getPtr()->validateActorAi(player, incoming);
        }

        void Do(ActorPacket &packet, Player &player, BaseActorList &actorList) override
        {
            Cell *serverCell = CellController::get()->getCell(&actorList.cell);

            if (serverCell != nullptr)
            {
                const std::string cellDescription
                    = actorList.cell.getShortDescription();
                const bool allowed = Script::CallBoolean<
                    Script::CallbackIdentity("OnActorAIIntent")>(
                        player.getId(), cellDescription.c_str());
                if (!allowed)
                    return;

                if (!Networking::getPtr()->commitActorAi(player, actorList))
                {
                    const char* reason = "canonical actor AI validation failed";
                    Script::Call<Script::CallbackIdentity(
                        "OnActorAIIntentRejected")>(player.getId(),
                            cellDescription.c_str(), reason);
                    return;
                }

                Networking* networking = Networking::getPtr();
                try
                {
                    Script::Call<Script::CallbackIdentity("OnActorAI")>(
                        player.getId(), cellDescription.c_str());
                }
                catch (...)
                {
                    networking->finishActorAiIntent(player);
                    throw;
                }

                if (!networking->finishActorAiIntent(player))
                    serverCell->sendToLoaded(&packet, &actorList);
            }
        }
    };
}

#endif //OPENMW_PROCESSORACTORAI_HPP
