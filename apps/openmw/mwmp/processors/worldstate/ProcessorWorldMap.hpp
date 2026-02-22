#ifndef OPENMW_PROCESSORWORLDMAP_HPP
#define OPENMW_PROCESSORWORLDMAP_HPP

#include "../WorldstateProcessor.hpp"

namespace mwmp
{
    class ProcessorWorldMap final: public WorldstateProcessor
    {
    public:
        ProcessorWorldMap()
        {
            BPP_INIT(ID_WORLD_MAP)
        }

        void Do(WorldstatePacket &packet, Worldstate &worldstate) override
        {
            worldstate.setMapExplored();
        }
    };
}

#endif //OPENMW_PROCESSORWORLDMAP_HPP
