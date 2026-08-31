#include "ActorProcessor.hpp"
#include "Networking.hpp"

using namespace mwmp;

template<class T>
typename BasePacketProcessor<T>::processors_t BasePacketProcessor<T>::processors;

void ActorProcessor::Do(ActorPacket &packet, Player &player, BaseActorList &actorList)
{
    packet.Send(true);
}

bool ActorProcessor::Process(const mwmp::transport::ReceivedApplicationPacket& packet, BaseActorList &actorList)
{
    for (auto &processor : processors)
    {
        if (processor.first == static_cast<std::uint16_t>(packet.id))
        {
            Player *player = Players::getPlayer(mwmp::transport::TransportConnectionId(packet.sender.value));
            if (player == nullptr)
                return true;
            ActorPacket *myPacket = Networking::get().getActorPacketController()->GetPacket(static_cast<std::uint16_t>(packet.id));

            if (!processor.second->avoidReading)
            {
                BaseActorList decoded;
                decoded.guid = mwmp::transport::TransportConnectionId(packet.sender.value);
                decoded.isValid = true;
                myPacket->setActorList(&decoded);
                myPacket->Read(packet.payload);
                if (!decoded.isValid || !myPacket->isPacketValid())
                {
                    LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "Received %s that failed integrity check and was ignored!", processor.second->strPacketID.c_str());
                    return true;
                }
                if (!Networking::getPtr()->validateActorAuthority(decoded)
                    || !processor.second->Validate(*player, decoded))
                    return true;
                actorList = std::move(decoded);
            }
            else
            {
                actorList.cell.blank();
                actorList.baseActors.clear();
                actorList.guid = mwmp::transport::TransportConnectionId(packet.sender.value);
                actorList.isValid = true;
            }

            myPacket->setActorList(&actorList);
            processor.second->Do(*myPacket, *player, actorList);
            return true;
        }
    }
    return false;
}
