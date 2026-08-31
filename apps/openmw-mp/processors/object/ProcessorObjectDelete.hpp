#ifndef OPENMW_PROCESSOROBJECTDELETE_HPP
#define OPENMW_PROCESSOROBJECTDELETE_HPP

#include "../ObjectProcessor.hpp"
#include <apps/openmw-mp/Networking.hpp>

namespace mwmp
{
    class ProcessorObjectDelete : public ObjectProcessor
    {
    public:
        ProcessorObjectDelete()
        {
            BPP_INIT(ID_OBJECT_DELETE)
        }

        bool Validate(Player& player, const BaseObjectList& incoming) override
        {
            return Networking::getPtr()->validateObjectMutation(player, incoming,
                mechanics::ObjectMutationKind::Delete);
        }

        void Do(ObjectPacket &packet, Player &player, BaseObjectList &objectList) override
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Received %s from %s", strPacketID.c_str(), player.npc.mName.c_str());

            if (!ApplyCanonicalMutation(player, objectList, "ObjectDelete"))
                return;
            Script::Call<Script::CallbackIdentity("OnObjectDelete")>(player.getId(), objectList.cell.getShortDescription().c_str());
        }
    };
}

#endif //OPENMW_PROCESSOROBJECTDELETE_HPP
