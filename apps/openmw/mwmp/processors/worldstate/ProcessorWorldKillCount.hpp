#ifndef OPENMW_PROCESSORWORLDKILLCOUNT_HPP
#define OPENMW_PROCESSORWORLDKILLCOUNT_HPP

#include "../WorldstateProcessor.hpp"

namespace mwmp
{
    class ProcessorWorldKillCount final: public WorldstateProcessor
    {
    public:
        ProcessorWorldKillCount()
        {
            BPP_INIT(ID_WORLD_KILL_COUNT)
        }

        void Do(WorldstatePacket &packet, Worldstate &worldstate) override
        {
            mwmp::Main::get().getNetworking()->getWorldstate()->setKills();
        }
    };
}

#endif //OPENMW_PROCESSORWORLDKILLCOUNT_HPP
