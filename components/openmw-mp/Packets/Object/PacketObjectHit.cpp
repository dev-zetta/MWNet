#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketObjectHit.hpp"

using namespace mwmp;

PacketObjectHit::PacketObjectHit() : ObjectPacket()
{
    packetID = ID_OBJECT_HIT;
    hasCellData = true;
}

void PacketObjectHit::Packet(bool send)
{
    if (!PacketHeader(send))
        return;

    BaseObject baseObject;
    for (unsigned int i = 0; i < objectList->baseObjectCount; i++)
    {
        if (send)
            baseObject = objectList->baseObjects.at(i);

        Field(baseObject.isPlayer);

        if (baseObject.isPlayer)
            Field(baseObject.guid);
        else
            Object(baseObject, send);

        Field(baseObject.hittingActor.isPlayer);

        if (baseObject.hittingActor.isPlayer)
        {
            Field(baseObject.hittingActor.guid);
        }
        else
        {
            Field(baseObject.hittingActor.refId, true);
            Field(baseObject.hittingActor.refNum);
            Field(baseObject.hittingActor.mpNum);

            Field(baseObject.hittingActor.name);
        }

        Field(baseObject.hitAttack.success);

        if (baseObject.hitAttack.success)
        {
            Field(baseObject.hitAttack.damage);
            Field(baseObject.hitAttack.block);
            Field(baseObject.hitAttack.knockdown);
        }

        if (!send)
            objectList->baseObjects.push_back(baseObject);
    }
}
