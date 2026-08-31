#include "WorldstateProcessor.hpp"
#include "Networking.hpp"

using namespace mwmp;

template<class T>
typename BasePacketProcessor<T>::processors_t BasePacketProcessor<T>::processors;

void WorldstateProcessor::Do(WorldstatePacket &packet, Player &player, BaseWorldstate &worldstate)
{
    packet.Send(true);
}

bool WorldstateProcessor::Process(RakNet::Packet &packet, BaseWorldstate &worldstate)
{
    for (auto &processor : processors)
    {
        if (processor.first == packet.data[0])
        {
            Player *player = Players::getPlayer(packet.guid);
            if (player == nullptr)
                return true;
            WorldstatePacket *myPacket = Networking::get().getWorldstatePacketController()->GetPacket(packet.data[0]);

            if (!processor.second->avoidReading)
            {
                BaseWorldstate decoded = worldstate;
                decoded.guid = packet.guid;
                decoded.isValid = true;
                myPacket->setWorldstate(&decoded);
                myPacket->Read();
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
                worldstate.guid = packet.guid;
                worldstate.isValid = true;
            }

            myPacket->setWorldstate(&worldstate);
            processor.second->Do(*myPacket, *player, worldstate);
            return true;
        }
    }
    return false;
}
