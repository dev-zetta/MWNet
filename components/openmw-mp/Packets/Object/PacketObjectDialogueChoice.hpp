#ifndef OPENMW_PACKETOBJECTDIALOGUECHOICE_HPP
#define OPENMW_PACKETOBJECTDIALOGUECHOICE_HPP

#include <components/openmw-mp/Packets/Object/ObjectPacket.hpp>

namespace mwmp
{
    class PacketObjectDialogueChoice : public ObjectPacket
    {
    public:
        PacketObjectDialogueChoice();

        virtual void Object(BaseObject& baseObject, bool send);
    };
}

#endif //OPENMW_PACKETOBJECTDIALOGUECHOICE_HPP
