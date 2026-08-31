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

        void Do(ActorPacket&, Player&, BaseActorList& actorList) override
        {
            // Protocol 11 actor authorities only acknowledge local expiry.
            // Canonical effect state is advanced and broadcast by the server.
            for (BaseActor& actor : actorList.baseActors)
                actor.spellsActiveChanges = {};
        }
    };
}

#endif //OPENMW_PROCESSORACTORSPELLSACTIVE_HPP
