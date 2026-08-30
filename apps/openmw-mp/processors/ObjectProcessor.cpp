#include "ObjectProcessor.hpp"
#include "Networking.hpp"

using namespace mwmp;

template<class T>
typename BasePacketProcessor<T>::processors_t BasePacketProcessor<T>::processors;

void ObjectProcessor::Do(ObjectPacket &packet, Player &player, BaseObjectList &objectList)
{
    packet.Send(true);
}

bool ObjectProcessor::Process(RakNet::Packet &packet, BaseObjectList &objectList) noexcept
{
    for (auto &processor : processors)
    {
        if (processor.first == packet.data[0])
        {
            Player *player = Players::getPlayer(packet.guid);
            if (player == nullptr)
                return true;
            ObjectPacket *myPacket = Networking::get().getObjectPacketController()->GetPacket(packet.data[0]);

            if (!processor.second->avoidReading)
            {
                BaseObjectList decoded;
                decoded.guid = packet.guid;
                decoded.isValid = true;
                myPacket->setObjectList(&decoded);
                myPacket->Read();
                if (!decoded.isValid || !myPacket->isPacketValid())
                {
                    LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "Received %s that failed integrity check and was ignored!", processor.second->strPacketID.c_str());
                    return true;
                }
                objectList = std::move(decoded);
            }
            else
            {
                objectList.cell.blank();
                objectList.baseObjects.clear();
                objectList.guid = packet.guid;
                objectList.isValid = true;
            }

            myPacket->setObjectList(&objectList);
            processor.second->Do(*myPacket, *player, objectList);
            return true;
        }
    }
    return false;
}
