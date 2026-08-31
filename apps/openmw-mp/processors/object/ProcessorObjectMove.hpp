#ifndef OPENMW_PROCESSOROBJECTMOVE_HPP
#define OPENMW_PROCESSOROBJECTMOVE_HPP

#include "../ObjectProcessor.hpp"
#include <apps/openmw-mp/Networking.hpp>

namespace mwmp
{
    class ProcessorObjectMove : public ObjectProcessor
    {
    public:
        ProcessorObjectMove()
        {
            BPP_INIT(ID_OBJECT_MOVE)
        }

        bool Validate(Player& player, const BaseObjectList& incoming) override
        {
            return Networking::getPtr()->validateObjectMutation(player, incoming,
                mechanics::ObjectMutationKind::Move);
        }

        void Do(ObjectPacket &packet, Player &player, BaseObjectList &objectList) override
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Received %s from %s",
                strPacketID.c_str(), player.npc.mName.c_str());
            if (!ApplyCanonicalMutation(player, objectList, "ObjectMove"))
                return;
            const std::string cellDescription = objectList.cell.getShortDescription();
            const char* packetType = "ObjectMove";
            Script::Call<Script::CallbackIdentity("OnObjectMutationCommitted")>(
                player.getId(), cellDescription.c_str(), packetType);
            packet.Send(true);
        }
    };
}

#endif //OPENMW_PROCESSOROBJECTMOVE_HPP
