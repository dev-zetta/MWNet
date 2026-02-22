#ifndef OPENMW_PROCESSORRECORDDYNAMIC_HPP
#define OPENMW_PROCESSORRECORDDYNAMIC_HPP

#include "../WorldstateProcessor.hpp"

namespace mwmp
{
    class ProcessorRecordDynamic final: public WorldstateProcessor
    {
    public:
        ProcessorRecordDynamic()
        {
            BPP_INIT(ID_RECORD_DYNAMIC)
        }

        void Do(WorldstatePacket &packet, Worldstate &worldstate) override
        {
            worldstate.addRecords();
        }
    };
}

#endif //OPENMW_PROCESSORRECORDDYNAMIC_HPP
