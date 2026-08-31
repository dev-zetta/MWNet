#ifndef OPENMW_PROCESSOROBJECTSPAWN_HPP
#define OPENMW_PROCESSOROBJECTSPAWN_HPP

#include "../ObjectProcessor.hpp"
#include <apps/openmw-mp/Networking.hpp>

namespace mwmp
{
    class ProcessorObjectSpawn : public ObjectProcessor
    {
    public:
        ProcessorObjectSpawn()
        {
            BPP_INIT(ID_OBJECT_SPAWN)
        }

        bool Validate(Player& player, const BaseObjectList& incoming) override
        {
            return Networking::getPtr()->validateObjectSpawn(player, incoming);
        }

        void Do(ObjectPacket &packet, Player &player, BaseObjectList &objectList) override
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Received %s from %s", strPacketID.c_str(), player.npc.mName.c_str());

            Networking* networking = Networking::getPtr();
            if (!networking->prepareObjectMutationIds(player, objectList))
            {
                networking->cancelObjectMutation(player);
                return;
            }
            if (!ApplyCanonicalMutation(player, objectList, "ObjectSpawn"))
                return;
            Script::Call<Script::CallbackIdentity("OnObjectSpawn")>(player.getId(), objectList.cell.getShortDescription().c_str());
        }
    };
}

#endif //OPENMW_PROCESSOROBJECTSPAWN_HPP
