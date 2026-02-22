#ifndef OPENMW_PROCESSOROBJECTRESTOCK_HPP
#define OPENMW_PROCESSOROBJECTRESTOCK_HPP

#include "BaseObjectProcessor.hpp"

namespace mwmp
{
    class ProcessorObjectRestock final: public BaseObjectProcessor
    {
    public:
        ProcessorObjectRestock()
        {
            BPP_INIT(ID_OBJECT_RESTOCK)
        }

        void Do(ObjectPacket &packet, ObjectList &objectList) override
        {
            BaseObjectProcessor::Do(packet, objectList);

            ptrCellStore = Main::get().getCellController()->getCellStore(objectList.cell);

            if (!ptrCellStore) return;

            objectList.restockObjects(ptrCellStore);
        }
    };
}

#endif //OPENMW_PROCESSOROBJECTRESTOCK_HPP
