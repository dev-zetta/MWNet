#include "../Main.hpp"
#include "../Networking.hpp"

#include "ObjectProcessor.hpp"

using namespace mwmp;

template<class T>
typename BasePacketProcessor<T>::processors_t BasePacketProcessor<T>::processors;

ObjectProcessor::~ObjectProcessor()
{

}

bool ObjectProcessor::Process(RakNet::Packet &packet, ObjectList &objectList)
{
    if (packet.length < BasePacket::headerSize())
        return false;

    RakNet::BitStream bsIn(&packet.data[1], packet.length - 1, false);
    if (!bsIn.Read(guid))
        return false;
    objectList.guid = guid;

    ObjectPacket *myPacket = Main::get().getNetworking()->getObjectPacket(packet.data[0]);

    myPacket->setObjectList(&objectList);
    myPacket->SetReadStream(&bsIn);

    for (auto &processor: processors)
    {
        if (processor.first == packet.data[0])
        {
            myGuid = Main::get().getLocalPlayer()->guid;
            request = packet.length == myPacket->headerSize();

            objectList.isValid = true;

            if (!request && !processor.second->avoidReading)
                myPacket->Read();

            if (objectList.isValid && (processor.second->avoidReading || request || myPacket->isPacketValid()))
                processor.second->Do(*myPacket, objectList);
            else
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "Received %s that failed integrity check and was ignored!", processor.second->strPacketID.c_str());

            return true;
        }
    }
    return false;
}
