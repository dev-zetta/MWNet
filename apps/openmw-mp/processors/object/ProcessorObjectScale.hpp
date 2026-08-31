#ifndef OPENMW_PROCESSOROBJECTSCALE_HPP
#define OPENMW_PROCESSOROBJECTSCALE_HPP

#include "../ObjectProcessor.hpp"
#include <apps/openmw-mp/Networking.hpp>

namespace mwmp
{
    class ProcessorObjectScale : public ObjectProcessor
    {
    public:
        ProcessorObjectScale()
        {
            BPP_INIT(ID_OBJECT_SCALE)
        }

        bool Validate(Player& player, const BaseObjectList& incoming) override
        {
            return Networking::getPtr()->validateObjectMutation(player, incoming,
                mechanics::ObjectMutationKind::Scale);
        }

        void Do(ObjectPacket &packet, Player &player, BaseObjectList &objectList) override
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Received %s from %s", strPacketID.c_str(), player.npc.mName.c_str());
            if (!ApplyCanonicalMutation(player, objectList, "ObjectScale"))
                return;
            Script::Call<Script::CallbackIdentity("OnObjectScale")>(player.getId(), objectList.cell.getShortDescription().c_str());
        }
    };
}

#endif //OPENMW_PROCESSOROBJECTSCALE_HPP
