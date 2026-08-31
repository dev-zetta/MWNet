#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/TimedLog.hpp>
#include "PacketActorSpeech.hpp"

using namespace mwmp;

PacketActorSpeech::PacketActorSpeech() : ActorPacket()
{
    packetID = ID_ACTOR_SPEECH;
}

void PacketActorSpeech::Actor(BaseActor &actor, bool send)
{
    RW(actor.sound, send);
}
