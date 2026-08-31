#include "../Networking.hpp"
#include "PlayerProcessor.hpp"
#include "../Main.hpp"

using namespace mwmp;

template<class T>
typename BasePacketProcessor<T>::processors_t BasePacketProcessor<T>::processors;

PlayerProcessor::~PlayerProcessor()
{

}

bool PlayerProcessor::Process(mwmp::transport::ApplicationPacketFrame &packet)
{
    if (packet.length < BasePacket::headerSize())
        return false;

    RakNet::BitStream bsIn(&packet.data[1], packet.length - 1, false);
    std::uint64_t guidValue = 0;
    if (!bsIn.Read(guidValue))
        return false;
    guid = RakNet::RakNetGUID(guidValue);

    PlayerPacket *myPacket = Main::get().getNetworking()->getPlayerPacket(packet.data[0]);
    myPacket->SetReadStream(&bsIn);

    /*if (myPacket == 0)
    {
        // error: packet not found
    }*/

    for (auto &processor : processors)
    {
        if (processor.first == packet.data[0])
        {
            myGuid = Main::get().getLocalPlayer()->guid;
            request = packet.length == myPacket->headerSize();

            BasePlayer *player = 0;
            if (guid != myGuid)
                player = PlayerList::getPlayer(guid);
            else
                player = Main::get().getLocalPlayer();

            if (!request && !processor.second->avoidReading)
            {
                BasePlayer validation(guid);
                myPacket->setPlayer(&validation);
                myPacket->Read();
                if (!myPacket->isPacketValid())
                {
                    LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "Received %s that failed decoding and was ignored!",
                        processor.second->strPacketID.c_str());
                    return true;
                }
                if (player == nullptr)
                    player = PlayerList::newPlayer(guid);
                myPacket->setPlayer(player);
                myPacket->Read();
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
