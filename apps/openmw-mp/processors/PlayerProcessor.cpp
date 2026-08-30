#include "PlayerProcessor.hpp"
#include "Networking.hpp"

using namespace mwmp;

template<class T>
typename BasePacketProcessor<T>::processors_t BasePacketProcessor<T>::processors;

bool PlayerProcessor::Process(RakNet::Packet &packet) noexcept
{
    for (auto &processor : processors)
    {
        if (processor.first == packet.data[0])
        {
            Player *player = Players::getPlayer(packet.guid);
            if (player == nullptr)
                return true;
            PlayerPacket *myPacket = Networking::get().getPlayerPacketController()->GetPacket(packet.data[0]);
            myPacket->setPlayer(player);

            if (!processor.second->avoidReading)
                myPacket->Read();

            if (!processor.second->avoidReading && !myPacket->isPacketValid())
            {
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "Received %s that failed decoding and was ignored!",
                    processor.second->strPacketID.c_str());
                return true;
            }

            processor.second->Do(*myPacket, *player);
            return true;
        }
    }
    return false;
}
