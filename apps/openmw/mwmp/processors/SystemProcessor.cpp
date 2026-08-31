#include "../Networking.hpp"
#include "SystemProcessor.hpp"
#include "../Main.hpp"

using namespace mwmp;

template<class T>
typename BasePacketProcessor<T>::processors_t BasePacketProcessor<T>::processors;

SystemProcessor::~SystemProcessor()
{

}

bool SystemProcessor::Process(mwmp::transport::ApplicationPacketFrame &packet)
{
    if (packet.length < BasePacket::headerSize())
        return false;

    RakNet::BitStream bsIn(&packet.data[1], packet.length - 1, false);
    std::uint64_t guidValue = 0;
    if (!bsIn.Read(guidValue))
        return false;
    guid = RakNet::RakNetGUID(guidValue);

    SystemPacket *myPacket = Main::get().getNetworking()->getSystemPacket(packet.data[0]);
    myPacket->SetReadStream(&bsIn);

    /*if (myPacket == 0)
    {
        // error: packet not found
    }*/

    for (auto &processor : processors)
    {
        if (processor.first == packet.data[0])
        {
            myGuid = Main::get().getLocalSystem()->guid;
            request = packet.length == myPacket->headerSize();

            BaseSystem *system = 0;
            system = Main::get().getLocalSystem();

            if (!request && !processor.second->avoidReading && system != 0)
            {
                BaseSystem decoded = *system;
                myPacket->setSystem(&decoded);
                myPacket->Read();
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
