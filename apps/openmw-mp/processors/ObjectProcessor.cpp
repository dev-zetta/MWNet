#include "ObjectProcessor.hpp"
#include "Networking.hpp"

using namespace mwmp;

template<class T>
typename BasePacketProcessor<T>::processors_t BasePacketProcessor<T>::processors;

void ObjectProcessor::Do(ObjectPacket &packet, Player &player, BaseObjectList &objectList)
{
    packet.Send(true);
}

bool ObjectProcessor::ApplyCanonicalMutation(Player& player,
    const BaseObjectList& objectList, const char* packetType)
{
    Networking* networking = Networking::getPtr();
    const std::string cellDescription = objectList.cell.getShortDescription();
    bool allowed = false;
    try
    {
        allowed = Script::CallBoolean<Script::CallbackIdentity(
            "OnObjectMutationIntent")>(player.getId(), cellDescription.c_str(), packetType);
    }
    catch (...)
    {
        networking->cancelObjectMutation(player);
        throw;
    }
    if (!allowed)
    {
        networking->cancelObjectMutation(player);
        const char* reason = "denied by script";
        Script::Call<Script::CallbackIdentity("OnObjectMutationIntentRejected")>(
            player.getId(), cellDescription.c_str(), packetType, reason);
        return false;
    }
    if (networking->commitObjectMutation(player))
        return true;

    const char* reason = "canonical object mutation failed";
    Script::Call<Script::CallbackIdentity("OnObjectMutationIntentRejected")>(
        player.getId(), cellDescription.c_str(), packetType, reason);
    return false;
}

bool ObjectProcessor::Process(const mwmp::transport::ReceivedApplicationPacket& packet, BaseObjectList &objectList)
{
    for (auto &processor : processors)
    {
        if (processor.first == static_cast<std::uint16_t>(packet.id))
        {
            Player *player = Players::getPlayer(mwmp::transport::TransportConnectionId(packet.sender.value));
            if (player == nullptr)
                return true;
            ObjectPacket *myPacket = Networking::get().getObjectPacketController()->GetPacket(static_cast<std::uint16_t>(packet.id));

            if (!processor.second->avoidReading)
            {
                BaseObjectList decoded;
                decoded.guid = mwmp::transport::TransportConnectionId(packet.sender.value);
                decoded.isValid = true;
                myPacket->setObjectList(&decoded);
                myPacket->Read(packet.payload);
                if (!decoded.isValid || !myPacket->isPacketValid())
                {
                    LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "Received %s that failed integrity check and was ignored!", processor.second->strPacketID.c_str());
                    return true;
                }
                if (!processor.second->Validate(*player, decoded))
                    return true;
                objectList = std::move(decoded);
            }
            else
            {
                objectList.cell.blank();
                objectList.baseObjects.clear();
                objectList.guid = mwmp::transport::TransportConnectionId(packet.sender.value);
                objectList.isValid = true;
            }

            myPacket->setObjectList(&objectList);
            processor.second->Do(*myPacket, *player, objectList);
            return true;
        }
    }
    return false;
}
