#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketObjectDialogueChoice.hpp"

using namespace mwmp;

PacketObjectDialogueChoice::PacketObjectDialogueChoice() : ObjectPacket()
{
    packetID = ID_OBJECT_DIALOGUE_CHOICE;
    hasCellData = true;
}

void PacketObjectDialogueChoice::Object(BaseObject& baseObject, bool send)
{
    ObjectPacket::Object(baseObject, send);
    Field(baseObject.dialogueChoiceType);

    if (baseObject.dialogueChoiceType == DialogueChoiceType::TOPIC)
        Field(baseObject.topicId, true);

    Field(baseObject.guiId);
}
