#ifndef OPENMW_PROCESSORACTORSTATSDYNAMIC_HPP
#define OPENMW_PROCESSORACTORSTATSDYNAMIC_HPP

#include "../ActorProcessor.hpp"
#include "apps/openmw/mwmp/Main.hpp"
#include "apps/openmw/mwmp/CellController.hpp"

namespace mwmp
{
    class ProcessorActorStatsDynamic final: public ActorProcessor
    {
    public:
        ProcessorActorStatsDynamic()
        {
            BPP_INIT(ID_ACTOR_STATS_DYNAMIC);
        }

        void Do(ActorPacket &packet, ActorList &actorList) override
        {
            Main::get().getCellController()->readStatsDynamic(actorList);
        }
    };
}

#endif //OPENMW_PROCESSORACTORSTATSDYNAMIC_HPP
