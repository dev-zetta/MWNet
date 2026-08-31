#include "ActorProcessor.hpp"
#include "../Networking.hpp"
#include "../Main.hpp"

using namespace mwmp;

template<class T>
typename BasePacketProcessor<T>::processors_t BasePacketProcessor<T>::processors;

ActorProcessor::~ActorProcessor()
{

}

bool ActorProcessor::Process(const mwmp::transport::ReceivedApplicationPacket& packet, ActorList &actorList)
{
    guid = mwmp::transport::TransportConnectionId(packet.subject);

    ActorPacket *myPacket = Main::get().getNetworking()->getActorPacket(static_cast<std::uint16_t>(packet.id));

    for (auto &processor : processors)
    {
        if (processor.first == static_cast<std::uint16_t>(packet.id))
        {
            myGuid = Main::get().getLocalPlayer()->guid;
            request = packet.payload.empty();

            if (!request && !processor.second->avoidReading)
            {
                BaseActorList decoded;
                decoded.guid = guid;
                decoded.isValid = true;
                myPacket->setActorList(&decoded);
                myPacket->Read(packet.payload);
                if (!decoded.isValid || !myPacket->isPacketValid())
                {
                    LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "Received %s that failed integrity check and was ignored!", processor.second->strPacketID.c_str());
                    return true;
                }
                static_cast<BaseActorList&>(actorList) = std::move(decoded);
            }
            else
            {
                actorList.guid = guid;
                actorList.isValid = true;
            }

            myPacket->setActorList(&actorList);
            processor.second->Do(*myPacket, actorList);
            return true;
        }
    }
    return false;
}
