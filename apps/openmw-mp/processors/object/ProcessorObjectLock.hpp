#ifndef OPENMW_PROCESSOROBJECTLOCK_HPP
#define OPENMW_PROCESSOROBJECTLOCK_HPP

#include "../ObjectProcessor.hpp"
#include <apps/openmw-mp/Networking.hpp>

namespace mwmp
{
    class ProcessorObjectLock : public ObjectProcessor
    {
    public:
        ProcessorObjectLock()
        {
            BPP_INIT(ID_OBJECT_LOCK)
        }

        bool Validate(Player& player, const BaseObjectList& incoming) override
        {
            return Networking::getPtr()->validateObjectMutation(player, incoming,
                mechanics::ObjectMutationKind::SetLock);
        }

        void Do(ObjectPacket &packet, Player &player, BaseObjectList &objectList) override
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Received %s from %s", strPacketID.c_str(), player.npc.mName.c_str());
            if (!ApplyCanonicalMutation(player, objectList, "ObjectLock"))
                return;
            Script::Call<Script::CallbackIdentity("OnObjectLock")>(player.getId(), objectList.cell.getShortDescription().c_str());
        }
    };
}

#endif //OPENMW_PROCESSOROBJECTLOCK_HPP
