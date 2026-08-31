#ifndef OPENMW_OBJECTPROCESSOR_HPP
#define OPENMW_OBJECTPROCESSOR_HPP


#include <components/openmw-mp/Base/BasePacketProcessor.hpp>
#include <components/openmw-mp/Packets/BasePacket.hpp>
#include <components/openmw-mp/Packets/Object/ObjectPacket.hpp>
#include <components/openmw-mp/NetworkMessages.hpp>
#include "Script/Script.hpp"
#include "Player.hpp"

namespace mwmp
{
    class ObjectProcessor : public BasePacketProcessor<ObjectProcessor>
    {
    public:
        virtual ~ObjectProcessor() = default;

        virtual void Do(ObjectPacket &packet, Player &player, BaseObjectList &objectList);
        virtual bool Validate(Player&, const BaseObjectList&) { return true; }

        static bool Process(RakNet::Packet &packet, BaseObjectList &objectList);
    };
}

#endif //OPENMW_OBJECTPROCESSOR_HPP
