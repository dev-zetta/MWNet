#include "../Main.hpp"
#include "../Networking.hpp"

#include "ObjectProcessor.hpp"

using namespace mwmp;

template<class T>
typename BasePacketProcessor<T>::processors_t BasePacketProcessor<T>::processors;

ObjectProcessor::~ObjectProcessor()
{

}

bool ObjectProcessor::Process(mwmp::transport::ApplicationPacketFrame &packet, ObjectList &objectList)
{
    if (packet.length < BasePacket::headerSize())
        return false;

    RakNet::BitStream bsIn(&packet.data[1], packet.length - 1, false);
    std::uint64_t guidValue = 0;
    if (!bsIn.Read(guidValue))
        return false;
    guid = mwmp::transport::TransportConnectionId(guidValue);

    ObjectPacket *myPacket = Main::get().getNetworking()->getObjectPacket(packet.data[0]);
    myPacket->SetReadStream(&bsIn);

    for (auto &processor: processors)
    {
        if (processor.first == packet.data[0])
        {
            myGuid = Main::get().getLocalPlayer()->guid;
            request = packet.length == myPacket->headerSize();

            if (!request && !processor.second->avoidReading)
            {
                BaseObjectList decoded;
                decoded.guid = guid;
                decoded.isValid = true;
                myPacket->setObjectList(&decoded);
                myPacket->Read();
                if (!decoded.isValid || !myPacket->isPacketValid())
                {
                    LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "Received %s that failed integrity check and was ignored!", processor.second->strPacketID.c_str());
                    return true;
                }
                static_cast<BaseObjectList&>(objectList) = std::move(decoded);
            }
            else
            {
                objectList.guid = guid;
                objectList.isValid = true;
            }

            myPacket->setObjectList(&objectList);
            processor.second->Do(*myPacket, objectList);
            return true;
        }
    }
    return false;
}
