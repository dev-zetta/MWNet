#ifndef OPENMW_PROCESSORDOORSTATE_HPP
#define OPENMW_PROCESSORDOORSTATE_HPP

#include "../ObjectProcessor.hpp"
#include <apps/openmw-mp/Networking.hpp>

namespace mwmp
{
    class ProcessorDoorState : public ObjectProcessor
    {
    public:
        ProcessorDoorState()
        {
            BPP_INIT(ID_DOOR_STATE)
        }

        bool Validate(Player& player, const BaseObjectList& incoming) override
        {
            return Networking::getPtr()->validateObjectMutation(player, incoming,
                mechanics::ObjectMutationKind::SetDoorState);
        }

        void Do(ObjectPacket &packet, Player &player, BaseObjectList &objectList) override
        {
            if (!ApplyCanonicalMutation(player, objectList, "DoorState"))
                return;
            Script::Call<Script::CallbackIdentity("OnDoorState")>(
                player.getId(), objectList.cell.getShortDescription().c_str());
        }
    };
}

#endif //OPENMW_PROCESSORDOORSTATE_HPP
