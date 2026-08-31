#include "PlayerProcessor.hpp"
#include "Networking.hpp"

using namespace mwmp;

template<class T>
typename BasePacketProcessor<T>::processors_t BasePacketProcessor<T>::processors;

bool PlayerProcessor::Process(const mwmp::transport::ReceivedApplicationPacket& packet)
{
    for (auto &processor : processors)
    {
        if (processor.first == static_cast<std::uint16_t>(packet.id))
        {
            Player *player = Players::getPlayer(mwmp::transport::TransportConnectionId(packet.sender.value));
            if (player == nullptr)
                return true;
            PlayerPacket *myPacket = Networking::get().getPlayerPacketController()->GetPacket(static_cast<std::uint16_t>(packet.id));

            if (!processor.second->avoidReading)
            {
                BasePlayer validation(mwmp::transport::TransportConnectionId(packet.sender.value));
                myPacket->setPlayer(&validation);
                myPacket->Read(packet.payload);
                if (!myPacket->isPacketValid())
                {
                    LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "Received %s that failed decoding and was ignored!",
                        processor.second->strPacketID.c_str());
                    return true;
                }
                if (!processor.second->Validate(*player, validation))
                    return true;
                myPacket->setPlayer(player);
                myPacket->Read(packet.payload);
                if (!myPacket->isPacketValid())
                    return true;
            }

            myPacket->setPlayer(player);
            processor.second->Do(*myPacket, *player);
            return true;
        }
    }
    return false;
}
