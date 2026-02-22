#ifndef OPENMW_PROCESSORACTORATTACK_HPP
#define OPENMW_PROCESSORACTORATTACK_HPP

#include "../ActorProcessor.hpp"
#include "apps/openmw/mwmp/Main.hpp"
#include "apps/openmw/mwmp/CellController.hpp"

namespace mwmp
{
    class ProcessorActorAttack final: public ActorProcessor
    {
    public:
        ProcessorActorAttack()
        {
            BPP_INIT(ID_ACTOR_ATTACK);
        }

        void Do(ActorPacket &packet, ActorList &actorList) override
        {
            Main::get().getCellController()->readAttack(actorList);
        }
    };
}

#endif //OPENMW_PROCESSORACTORATTACK_HPP
