#ifndef OPENMW_SYSTEMPACKET_HPP
#define OPENMW_SYSTEMPACKET_HPP

#include <string>
#include <RakNetTypes.h>
#include <BitStream.h>
#include <PacketPriority.h>
#include <components/openmw-mp/Base/BaseSystem.hpp>

#include <components/openmw-mp/Packets/BasePacket.hpp>

namespace mwmp
{
    class SystemPacket : public BasePacket
    {
    public:
        SystemPacket();

        ~SystemPacket();

        void setSystem(BaseSystem *newSystem);
        BaseSystem *getSystem();

    protected:
        BaseSystem *system = nullptr;

    };
}

#endif //OPENMW_SYSTEMPACKET_HPP
