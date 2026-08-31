#ifndef OPENMW_PROCESSOROBJECTSTATE_HPP
#define OPENMW_PROCESSOROBJECTSTATE_HPP

#include "../ObjectProcessor.hpp"
#include <apps/openmw-mp/Networking.hpp>

namespace mwmp
{
    class ProcessorObjectState : public ObjectProcessor
    {
    public:
        ProcessorObjectState()
        {
            BPP_INIT(ID_OBJECT_STATE)
        }

        bool Validate(Player& player, const BaseObjectList& incoming) override
        {
            return Networking::getPtr()->validateObjectMutation(player, incoming,
                mechanics::ObjectMutationKind::SetEnabled);
        }

        void Do(ObjectPacket &packet, Player &player, BaseObjectList &objectList) override
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Received %s from %s", strPacketID.c_str(), player.npc.mName.c_str());
            if (!ApplyCanonicalMutation(player, objectList, "ObjectState"))
                return;
            Script::Call<Script::CallbackIdentity("OnObjectState")>(player.getId(), objectList.cell.getShortDescription().c_str());
        }
    };
}

#endif //OPENMW_PROCESSOROBJECTSTATE_HPP
