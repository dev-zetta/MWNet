#ifndef OPENMW_PROCESSORACTORSPELLSACTIVE_HPP
#define OPENMW_PROCESSORACTORSPELLSACTIVE_HPP

#include "../ActorProcessor.hpp"
#include "apps/openmw-mp/Networking.hpp"

namespace mwmp
{
    class ProcessorActorSpellsActive : public ActorProcessor
    {
    public:
        ProcessorActorSpellsActive()
        {
            BPP_INIT(ID_ACTOR_SPELLS_ACTIVE)
        }

        bool Validate(Player& player, const BaseActorList& incoming) override
        {
            return Networking::getPtr()->validateActorActiveEffects(player, incoming);
        }

        void Do(ActorPacket &packet, Player &player, BaseActorList &actorList) override
        {
            // Send only to players who have the cell loaded
            Cell *serverCell = CellController::get()->getCell(&actorList.cell);

            if (serverCell != nullptr && *serverCell->getAuthority() == actorList.guid)
            {
                const std::string cellDescription = actorList.cell.getShortDescription();
                const bool allowed = Script::CallBoolean<Script::CallbackIdentity(
                    "OnActorSpellsActiveIntent")>(player.getId(),
                    cellDescription.c_str());
                if (!allowed)
                    return;

                if (!Networking::getPtr()->commitActorActiveEffects(player, actorList))
                {
                    const char* reason = "canonical active-effect validation failed";
                    Script::Call<Script::CallbackIdentity(
                        "OnActorSpellsActiveIntentRejected")>(player.getId(),
                        cellDescription.c_str(), reason);
                    return;
                }

                Networking* networking = Networking::getPtr();
                try
                {
                    Script::Call<Script::CallbackIdentity("OnActorSpellsActive")>(
                        player.getId(), cellDescription.c_str());
                }
                catch (...)
                {
                    networking->finishActorActiveEffectIntent(player);
                    throw;
                }

                if (!networking->finishActorActiveEffectIntent(player))
                    serverCell->sendToLoaded(&packet, &actorList);
            }
        }
    };
}

#endif //OPENMW_PROCESSORACTORSPELLSACTIVE_HPP
