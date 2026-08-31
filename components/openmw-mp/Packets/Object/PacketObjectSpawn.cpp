#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketObjectSpawn.hpp"

using namespace mwmp;

PacketObjectSpawn::PacketObjectSpawn() : ObjectPacket()
{
    packetID = ID_OBJECT_SPAWN;
    hasCellData = true;
}

void PacketObjectSpawn::Object(BaseObject &baseObject, bool send)
{
    ObjectPacket::Object(baseObject, send);
    Field(baseObject.position);

    Field(baseObject.isSummon);

    if (baseObject.isSummon)
    {
        Field(baseObject.summonEffectId);
        Field(baseObject.summonSpellId, true);
        Field(baseObject.summonDuration);

        Field(baseObject.master.isPlayer);

        if (baseObject.master.isPlayer)
        {
            Field(baseObject.master.guid);
        }
        else
        {
            Field(baseObject.master.refId, true);
            Field(baseObject.master.refNum);
            Field(baseObject.master.mpNum);
        }
    }
}
