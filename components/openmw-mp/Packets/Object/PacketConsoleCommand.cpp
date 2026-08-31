#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketConsoleCommand.hpp"

using namespace mwmp;

PacketConsoleCommand::PacketConsoleCommand() : ObjectPacket()
{
    packetID = ID_CONSOLE_COMMAND;
    hasCellData = true;
}

void PacketConsoleCommand::Packet(bool send)
{
    if (!PacketHeader(send))
        return;

    if (!Field(objectList->consoleCommand, true, protocol::limits::commandBytes))
        return;

    BaseObject baseObject;
    for (unsigned int i = 0; i < objectList->baseObjectCount; i++)
    {
        if (send)
            baseObject = objectList->baseObjects.at(i);

        Field(baseObject.isPlayer);

        if (baseObject.isPlayer)
            Field(baseObject.guid);
        else
            Object(baseObject, send);

        if (!send)
            objectList->baseObjects.push_back(baseObject);
    }
}
