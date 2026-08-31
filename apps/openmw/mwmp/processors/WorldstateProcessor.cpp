#include "../Main.hpp"
#include "../Networking.hpp"

#include "WorldstateProcessor.hpp"

using namespace mwmp;

template<class T>
typename BasePacketProcessor<T>::processors_t BasePacketProcessor<T>::processors;

WorldstateProcessor::~WorldstateProcessor()
{

}

bool WorldstateProcessor::Process(const mwmp::transport::ReceivedApplicationPacket& packet, Worldstate &worldstate)
{
    guid = mwmp::transport::TransportConnectionId(packet.subject);

    WorldstatePacket *myPacket = Main::get().getNetworking()->getWorldstatePacket(static_cast<std::uint16_t>(packet.id));

    for (auto &processor : processors)
    {
        if (processor.first == static_cast<std::uint16_t>(packet.id))
        {
            myGuid = Main::get().getLocalPlayer()->guid;
            request = packet.payload.empty();

            if (!request && !processor.second->avoidReading)
            {
                BaseWorldstate decoded = worldstate;
                decoded.guid = guid;
                decoded.isValid = true;
                myPacket->setWorldstate(&decoded);
                myPacket->Read(packet.payload);
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
