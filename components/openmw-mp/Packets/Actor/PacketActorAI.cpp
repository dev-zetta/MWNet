#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/TimedLog.hpp>
#include "PacketActorAI.hpp"

using namespace mwmp;

PacketActorAI::PacketActorAI() : ActorPacket()
{
    packetID = ID_ACTOR_AI;
}

void PacketActorAI::Actor(BaseActor &actor, bool send)
{
    Field(actor.aiAction);

    if (actor.aiAction != mwmp::BaseActorList::CANCEL)
    {
        if (actor.aiAction == mwmp::BaseActorList::WANDER)
        {
            Field(actor.aiDistance);
            Field(actor.aiShouldRepeat);
        }

        if (actor.aiAction == mwmp::BaseActorList::ESCORT || actor.aiAction == mwmp::BaseActorList::WANDER)
            Field(actor.aiDuration);

        if (actor.aiAction == mwmp::BaseActorList::ESCORT || actor.aiAction == mwmp::BaseActorList::TRAVEL)
            Field(actor.aiCoordinates);

        if (actor.aiAction == mwmp::BaseActorList::ACTIVATE || actor.aiAction == mwmp::BaseActorList::COMBAT ||
            actor.aiAction == mwmp::BaseActorList::ESCORT || actor.aiAction == mwmp::BaseActorList::FOLLOW)
        {
            Field(actor.hasAiTarget);

            if (actor.hasAiTarget)
            {
                Field(actor.aiTarget.isPlayer);

                if (actor.aiTarget.isPlayer)
                {
                    Field(actor.aiTarget.guid);
                }
                else
                {
                    Field(actor.aiTarget.refId, true);
                    Field(actor.aiTarget.refNum);
                    Field(actor.aiTarget.mpNum);
                }
            }
        }
    }
}
