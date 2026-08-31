#include "../Main.hpp"
#include "../Networking.hpp"

#include "ObjectProcessor.hpp"

using namespace mwmp;

template<class T>
typename BasePacketProcessor<T>::processors_t BasePacketProcessor<T>::processors;

ObjectProcessor::~ObjectProcessor()
{

}

bool ObjectProcessor::Process(const mwmp::transport::ReceivedApplicationPacket& packet, ObjectList &objectList)
{
    guid = mwmp::transport::TransportConnectionId(packet.subject);

    ObjectPacket *myPacket = Main::get().getNetworking()->getObjectPacket(static_cast<std::uint16_t>(packet.id));

    for (auto &processor: processors)
    {
        if (processor.first == static_cast<std::uint16_t>(packet.id))
        {
            myGuid = Main::get().getLocalPlayer()->guid;
            request = packet.payload.empty();

            if (!request && !processor.second->avoidReading)
            {
                BaseObjectList decoded;
                decoded.guid = guid;
                decoded.isValid = true;
                myPacket->setObjectList(&decoded);
                myPacket->Read(packet.payload);
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
