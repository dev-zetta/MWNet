#include "ActorProcessor.hpp"
#include "Networking.hpp"

using namespace mwmp;

template<class T>
typename BasePacketProcessor<T>::processors_t BasePacketProcessor<T>::processors;

void ActorProcessor::Do(ActorPacket &packet, Player &player, BaseActorList &actorList)
{
    packet.Send(true);
}

bool ActorProcessor::Process(RakNet::Packet &packet, BaseActorList &actorList)
{
    for (auto &processor : processors)
    {
        if (processor.first == packet.data[0])
        {
            Player *player = Players::getPlayer(packet.guid);
            if (player == nullptr)
                return true;
            ActorPacket *myPacket = Networking::get().getActorPacketController()->GetPacket(packet.data[0]);

            if (!processor.second->avoidReading)
            {
                BaseActorList decoded;
                decoded.guid = packet.guid;
                decoded.isValid = true;
                myPacket->setActorList(&decoded);
                myPacket->Read();
                if (!decoded.isValid || !myPacket->isPacketValid())
                {
                    LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "Received %s that failed integrity check and was ignored!", processor.second->strPacketID.c_str());
                    return true;
                }
                actorList = std::move(decoded);
            }
            else
            {
                actorList.cell.blank();
                actorList.baseActors.clear();
                actorList.guid = packet.guid;
                actorList.isValid = true;
            }

            if (!Networking::getPtr()->validateActorAuthority(actorList))
                return true;

            myPacket->setActorList(&actorList);
            processor.second->Do(*myPacket, *player, actorList);
            return true;
        }
    }
    return false;
}
