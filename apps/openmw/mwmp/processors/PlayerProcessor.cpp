#include "../Networking.hpp"
#include "PlayerProcessor.hpp"
#include "../Main.hpp"

using namespace mwmp;

template<class T>
typename BasePacketProcessor<T>::processors_t BasePacketProcessor<T>::processors;

PlayerProcessor::~PlayerProcessor()
{

}

bool PlayerProcessor::Process(const mwmp::transport::ReceivedApplicationPacket& packet)
{
    guid = mwmp::transport::TransportConnectionId(packet.subject);

    PlayerPacket *myPacket = Main::get().getNetworking()->getPlayerPacket(static_cast<std::uint16_t>(packet.id));

    /*if (myPacket == 0)
    {
        // error: packet not found
    }*/

    for (auto &processor : processors)
    {
        if (processor.first == static_cast<std::uint16_t>(packet.id))
        {
            myGuid = Main::get().getLocalPlayer()->guid;
            request = packet.payload.empty();

            BasePlayer *player = 0;
            if (guid != myGuid)
                player = PlayerList::getPlayer(guid);
            else
                player = Main::get().getLocalPlayer();

            if (!request && !processor.second->avoidReading)
            {
                BasePlayer validation(guid);
                myPacket->setPlayer(&validation);
                myPacket->Read(packet.payload);
                if (!myPacket->isPacketValid())
                {
                    LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "Received %s that failed decoding and was ignored!",
                        processor.second->strPacketID.c_str());
                    return true;
                }
                if (player == nullptr)
                    player = PlayerList::newPlayer(guid);
                myPacket->setPlayer(player);
                myPacket->Read(packet.payload);
                if (!myPacket->isPacketValid())
                    return true;
            }

            if (player != nullptr)
                myPacket->setPlayer(player);
            processor.second->Do(*myPacket, player);
            return true;
        }
    }
    return false;
}
