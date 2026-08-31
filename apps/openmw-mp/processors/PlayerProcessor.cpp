#include "PlayerProcessor.hpp"
#include "Networking.hpp"

using namespace mwmp;

template<class T>
typename BasePacketProcessor<T>::processors_t BasePacketProcessor<T>::processors;

bool PlayerProcessor::Process(mwmp::transport::ApplicationPacketFrame &packet)
{
    for (auto &processor : processors)
    {
        if (processor.first == packet.data[0])
        {
            Player *player = Players::getPlayer(RakNet::RakNetGUID(packet.sender.value));
            if (player == nullptr)
                return true;
            PlayerPacket *myPacket = Networking::get().getPlayerPacketController()->GetPacket(packet.data[0]);

            if (!processor.second->avoidReading)
            {
                BasePlayer validation(RakNet::RakNetGUID(packet.sender.value));
                myPacket->setPlayer(&validation);
                myPacket->Read();
                if (!myPacket->isPacketValid())
                {
                    LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "Received %s that failed decoding and was ignored!",
                        processor.second->strPacketID.c_str());
                    return true;
                }
                if (!processor.second->Validate(*player, validation))
                    return true;
                myPacket->setPlayer(player);
                myPacket->Read();
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
