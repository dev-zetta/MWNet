#include "../Networking.hpp"
#include "SystemProcessor.hpp"
#include "../Main.hpp"

using namespace mwmp;

template<class T>
typename BasePacketProcessor<T>::processors_t BasePacketProcessor<T>::processors;

SystemProcessor::~SystemProcessor()
{

}

bool SystemProcessor::Process(RakNet::Packet &packet)
{
    if (packet.length < BasePacket::headerSize())
        return false;

    RakNet::BitStream bsIn(&packet.data[1], packet.length - 1, false);
    if (!bsIn.Read(guid))
        return false;

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
                myPacket->setSystem(system);
                myPacket->Read();
                if (!myPacket->isPacketValid())
                {
                    LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "Received %s that failed decoding and was ignored!",
                        processor.second->strPacketID.c_str());
                    return true;
                }
            }

            processor.second->Do(*myPacket, system);
            return true;
        }
    }
    return false;
}
