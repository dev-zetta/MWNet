#include "ActorProcessor.hpp"
#include "../Networking.hpp"
#include "../Main.hpp"

using namespace mwmp;

template<class T>
typename BasePacketProcessor<T>::processors_t BasePacketProcessor<T>::processors;

ActorProcessor::~ActorProcessor()
{

}

bool ActorProcessor::Process(RakNet::Packet &packet, ActorList &actorList)
{
    if (packet.length < BasePacket::headerSize())
        return false;

    RakNet::BitStream bsIn(&packet.data[1], packet.length - 1, false);
    std::uint64_t guidValue = 0;
    if (!bsIn.Read(guidValue))
        return false;
    guid = RakNet::RakNetGUID(guidValue);

    ActorPacket *myPacket = Main::get().getNetworking()->getActorPacket(packet.data[0]);
    myPacket->SetReadStream(&bsIn);

    for (auto &processor : processors)
    {
        if (processor.first == packet.data[0])
        {
            myGuid = Main::get().getLocalPlayer()->guid;
            request = packet.length == myPacket->headerSize();

            if (!request && !processor.second->avoidReading)
            {
                BaseActorList decoded;
                decoded.guid = guid;
                decoded.isValid = true;
                myPacket->setActorList(&decoded);
                myPacket->Read();
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
