#ifndef OPENMW_PROCESSORACTORDEATH_HPP
#define OPENMW_PROCESSORACTORDEATH_HPP

#include "../ActorProcessor.hpp"
#include "apps/openmw-mp/Networking.hpp"

namespace mwmp
{
    class ProcessorActorDeath : public ActorProcessor
    {
    public:
        ProcessorActorDeath()
        {
            BPP_INIT(ID_ACTOR_DEATH)
        }

        bool Validate(Player& player, const BaseActorList& incoming) override
        {
            Networking::getPtr()->rejectActorDeathClaims(player, incoming);
            return false;
        }
    };
}

#endif //OPENMW_PROCESSORACTORDEATH_HPP
