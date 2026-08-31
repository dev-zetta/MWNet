#ifndef OPENMW_PROCESSOROBJECTACTIVATE_HPP
#define OPENMW_PROCESSOROBJECTACTIVATE_HPP

#include "../ObjectProcessor.hpp"
#include <apps/openmw-mp/Networking.hpp>

namespace mwmp
{
    class ProcessorObjectActivate : public ObjectProcessor
    {
    public:
        ProcessorObjectActivate()
        {
            BPP_INIT(ID_OBJECT_ACTIVATE)
        }

        bool Validate(Player& player, const BaseObjectList& incoming) override
        {
            return Networking::getPtr()->validateObjectActivation(player, incoming);
        }

        void Do(ObjectPacket &packet, Player &player, BaseObjectList &objectList) override
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Received %s from %s", strPacketID.c_str(), player.npc.mName.c_str());

            if (!ApplyCanonicalMutation(player, objectList, "ObjectActivate"))
                return;
            Script::Call<Script::CallbackIdentity("OnObjectActivate")>(player.getId(), objectList.cell.getShortDescription().c_str());
        }
    };
}

#endif //OPENMW_PROCESSOROBJECTACTIVATE_HPP
