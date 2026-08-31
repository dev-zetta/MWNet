#ifndef OPENMW_PROCESSOROBJECTROTATE_HPP
#define OPENMW_PROCESSOROBJECTROTATE_HPP

#include "../ObjectProcessor.hpp"
#include <apps/openmw-mp/Networking.hpp>

namespace mwmp
{
    class ProcessorObjectRotate : public ObjectProcessor
    {
    public:
        ProcessorObjectRotate()
        {
            BPP_INIT(ID_OBJECT_ROTATE)
        }

        bool Validate(Player& player, const BaseObjectList& incoming) override
        {
            return Networking::getPtr()->validateObjectMutation(player, incoming,
                mechanics::ObjectMutationKind::Rotate);
        }

        void Do(ObjectPacket &packet, Player &player, BaseObjectList &objectList) override
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Received %s from %s",
                strPacketID.c_str(), player.npc.mName.c_str());
            if (!ApplyCanonicalMutation(player, objectList, "ObjectRotate"))
                return;
            const std::string cellDescription = objectList.cell.getShortDescription();
            const char* packetType = "ObjectRotate";
            Script::Call<Script::CallbackIdentity("OnObjectMutationCommitted")>(
                player.getId(), cellDescription.c_str(), packetType);
            packet.Send(true);
        }
    };
}

#endif //OPENMW_PROCESSOROBJECTROTATE_HPP
