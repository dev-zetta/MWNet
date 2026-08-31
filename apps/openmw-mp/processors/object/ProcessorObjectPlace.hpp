#ifndef OPENMW_PROCESSOROBJECTPLACE_HPP
#define OPENMW_PROCESSOROBJECTPLACE_HPP

#include "../ObjectProcessor.hpp"
#include <apps/openmw-mp/Networking.hpp>

namespace mwmp
{
    class ProcessorObjectPlace : public ObjectProcessor
    {
    public:
        ProcessorObjectPlace()
        {
            BPP_INIT(ID_OBJECT_PLACE)
        }

        bool Validate(Player& player, const BaseObjectList& incoming) override
        {
            return Networking::getPtr()->validateObjectPlace(player, incoming);
        }

        void Do(ObjectPacket &packet, Player &player, BaseObjectList &objectList) override
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Received %s from %s", strPacketID.c_str(), player.npc.mName.c_str());

            Networking* networking = Networking::getPtr();
            const std::string cellDescription = objectList.cell.getShortDescription();
            if (!networking->prepareObjectPlace(player, objectList))
            {
                networking->cancelObjectPlace(player);
                return;
            }

            bool allowed = false;
            try
            {
                allowed = Script::CallBoolean<Script::CallbackIdentity(
                    "OnObjectPlaceIntent")>(player.getId(), cellDescription.c_str());
            }
            catch (...)
            {
                networking->cancelObjectPlace(player);
                throw;
            }
            if (!allowed)
            {
                networking->cancelObjectPlace(player);
                return;
            }
            if (!networking->commitObjectPlace(player))
            {
                const char* reason = "canonical object placement failed";
                Script::Call<Script::CallbackIdentity(
                    "OnObjectPlaceIntentRejected")>(player.getId(),
                    cellDescription.c_str(), reason);
                return;
            }

            Script::Call<Script::CallbackIdentity("OnObjectPlace")>(
                player.getId(), cellDescription.c_str());
        }
    };
}

#endif //OPENMW_PROCESSOROBJECTPLACE_HPP
