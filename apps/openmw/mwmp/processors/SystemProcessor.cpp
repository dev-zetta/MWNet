#include "../Networking.hpp"
#include "SystemProcessor.hpp"
#include "../Main.hpp"

using namespace mwmp;

template<class T>
typename BasePacketProcessor<T>::processors_t BasePacketProcessor<T>::processors;

SystemProcessor::~SystemProcessor()
{

}

bool SystemProcessor::Process(const mwmp::transport::ReceivedApplicationPacket& packet)
{
    guid = mwmp::transport::TransportConnectionId(packet.subject);

    SystemPacket *myPacket = Main::get().getNetworking()->getSystemPacket(static_cast<std::uint16_t>(packet.id));

    /*if (myPacket == 0)
    {
        // error: packet not found
    }*/

    for (auto &processor : processors)
    {
        if (processor.first == static_cast<std::uint16_t>(packet.id))
        {
            myGuid = Main::get().getLocalSystem()->guid;
            request = packet.payload.empty();

            BaseSystem *system = 0;
            system = Main::get().getLocalSystem();

            if (!request && !processor.second->avoidReading && system != 0)
            {
                BaseSystem decoded = *system;
                myPacket->setSystem(&decoded);
                myPacket->Read(packet.payload);
                if (!myPacket->isPacketValid())
                {
                    LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "Received %s that failed decoding and was ignored!",
                        processor.second->strPacketID.c_str());
                    return true;
                }
                *system = std::move(decoded);
            }

            if (system != nullptr)
                myPacket->setSystem(system);
            processor.second->Do(*myPacket, system);
            return true;
        }
    }
    return false;
}
