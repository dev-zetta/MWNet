#include "WorldstateProcessor.hpp"
#include "Networking.hpp"

using namespace mwmp;

template<class T>
typename BasePacketProcessor<T>::processors_t BasePacketProcessor<T>::processors;

void WorldstateProcessor::Do(WorldstatePacket &packet, Player &player, BaseWorldstate &worldstate)
{
    packet.Send(true);
}

bool WorldstateProcessor::Process(const mwmp::transport::ReceivedApplicationPacket& packet, BaseWorldstate &worldstate)
{
    for (auto &processor : processors)
    {
        if (processor.first == static_cast<std::uint16_t>(packet.id))
        {
            Player *player = Players::getPlayer(mwmp::transport::TransportConnectionId(packet.sender.value));
            if (player == nullptr)
                return true;
            WorldstatePacket *myPacket = Networking::get().getWorldstatePacketController()->GetPacket(static_cast<std::uint16_t>(packet.id));

            if (!processor.second->avoidReading)
            {
                BaseWorldstate decoded = worldstate;
                decoded.guid = mwmp::transport::TransportConnectionId(packet.sender.value);
                decoded.isValid = true;
                myPacket->setWorldstate(&decoded);
                myPacket->Read(packet.payload);
                if (!decoded.isValid || !myPacket->isPacketValid())
                {
                    LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "Received %s that failed integrity check and was ignored!", processor.second->strPacketID.c_str());
                    return true;
                }
                if (!processor.second->Validate(*player, decoded))
                    return true;
                worldstate = std::move(decoded);
            }
            else
            {
                worldstate.guid = mwmp::transport::TransportConnectionId(packet.sender.value);
                worldstate.isValid = true;
            }

            myPacket->setWorldstate(&worldstate);
            processor.second->Do(*myPacket, *player, worldstate);
            return true;
        }
    }
    return false;
}
