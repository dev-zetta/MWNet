#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/TimedLog.hpp>
#include "PacketContainer.hpp"

using namespace mwmp;

PacketContainer::PacketContainer() : ObjectPacket()
{
    packetID = ID_CONTAINER;
    hasCellData = true;
}

void PacketContainer::Packet(bool send)
{
    if (!PacketHeader(send))
        return;

    Field(objectList->action);
    Field(objectList->containerSubAction);

    BaseObject baseObject;
    for (unsigned int i = 0; i < objectList->baseObjectCount; i++)
    {
        if (send)
        {
            baseObject = objectList->baseObjects.at(i);
            baseObject.containerItemCount = (unsigned int) (baseObject.containerItems.size());
        }
        else
            baseObject.containerItems.clear();

        Object(baseObject, send);

        if (!CollectionSize(baseObject.containerItemCount, protocol::limits::objectChanges)
            || baseObject.refId.empty() || (baseObject.refNum != 0 && baseObject.mpNum != 0))
        {
            objectList->isValid = false;
            return;
        }

        ContainerItem containerItem;

        for (unsigned int j = 0; j < baseObject.containerItemCount; j++)
        {
            if (send)
                containerItem = baseObject.containerItems.at(j);

            Field(containerItem.refId, true);
            Field(containerItem.count);
            Field(containerItem.charge);
            Field(containerItem.enchantmentCharge);
            Field(containerItem.soul, true);
            Field(containerItem.actionCount);

            if (!send)
                baseObject.containerItems.push_back(containerItem);
        }
        if (!send)
            objectList->baseObjects.push_back(baseObject);
    }
}
