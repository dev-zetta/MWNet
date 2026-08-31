#include "../Main.hpp"
#include "../Networking.hpp"

#include "WorldstateProcessor.hpp"

using namespace mwmp;

template<class T>
typename BasePacketProcessor<T>::processors_t BasePacketProcessor<T>::processors;

WorldstateProcessor::~WorldstateProcessor()
{

}

bool WorldstateProcessor::Process(mwmp::transport::ApplicationPacketFrame &packet, Worldstate &worldstate)
{
    if (packet.length < BasePacket::headerSize())
        return false;

    RakNet::BitStream bsIn(&packet.data[1], packet.length - 1, false);
    std::uint64_t guidValue = 0;
    if (!bsIn.Read(guidValue))
        return false;
    guid = mwmp::transport::TransportConnectionId(guidValue);

    WorldstatePacket *myPacket = Main::get().getNetworking()->getWorldstatePacket(packet.data[0]);
    myPacket->SetReadStream(&bsIn);

    for (auto &processor : processors)
    {
        if (processor.first == packet.data[0])
        {
            myGuid = Main::get().getLocalPlayer()->guid;
            request = packet.length == myPacket->headerSize();

            if (!request && !processor.second->avoidReading)
            {
                BaseWorldstate decoded = worldstate;
                decoded.guid = guid;
                decoded.isValid = true;
                myPacket->setWorldstate(&decoded);
                myPacket->Read();
                if (!decoded.isValid || !myPacket->isPacketValid())
                {
                    LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "Received %s that failed integrity check and was ignored!", processor.second->strPacketID.c_str());
                    return true;
                }
                static_cast<BaseWorldstate&>(worldstate) = std::move(decoded);
            }
            else
            {
                worldstate.guid = guid;
                worldstate.isValid = true;
            }

            myPacket->setWorldstate(&worldstate);
            processor.second->Do(*myPacket, worldstate);
            return true;
        }
    }
    return false;
}
