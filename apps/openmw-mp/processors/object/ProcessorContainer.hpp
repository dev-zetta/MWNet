#ifndef OPENMW_PROCESSORCONTAINER_HPP
#define OPENMW_PROCESSORCONTAINER_HPP

#include "../ObjectProcessor.hpp"
#include "apps/openmw-mp/Networking.hpp"

namespace mwmp
{
    class ProcessorContainer : public ObjectProcessor
    {
    public:
        ProcessorContainer()
        {
            BPP_INIT(ID_CONTAINER)
        }

        bool Validate(Player& player, const BaseObjectList& incoming) override
        {
            return Networking::getPtr()->validateContainerAction(player, incoming);
        }

        void Do(ObjectPacket &packet, Player &player, BaseObjectList &objectList) override
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Received %s from %s", strPacketID.c_str(), player.npc.mName.c_str());
            LOG_APPEND(TimedLog::LOG_INFO, "- action: %i", objectList.action);

            const std::string cellDescription = objectList.cell.getShortDescription();
            const bool allowed = Script::CallBoolean<Script::CallbackIdentity(
                "OnContainerIntent")>(player.getId(), cellDescription.c_str());
            if (!allowed)
                return;

            if (!Networking::getPtr()->commitContainerAction(player, objectList))
            {
                const char* reason = "canonical inventory validation failed";
                Script::Call<Script::CallbackIdentity(
                    "OnContainerIntentRejected")>(player.getId(), cellDescription.c_str(),
                    reason);
                return;
            }

            Script::Call<Script::CallbackIdentity("OnContainer")>(
                player.getId(), cellDescription.c_str());

            LOG_APPEND(TimedLog::LOG_INFO, "- Finished processing ID_CONTAINER");
        }
    };
}

#endif //OPENMW_PROCESSORCONTAINER_HPP
