#ifndef OPENMW_WORLDSTATEPACKET_HPP
#define OPENMW_WORLDSTATEPACKET_HPP

#include <string>
#include <components/openmw-mp/Base/BaseWorldstate.hpp>

#include <components/openmw-mp/Packets/BasePacket.hpp>

namespace mwmp
{
    class WorldstatePacket : public BasePacket
    {
    public:
        WorldstatePacket();

        ~WorldstatePacket();

        void setWorldstate(BaseWorldstate *newWorldstate);
        BaseWorldstate *getWorldstate();

    protected:
        BaseWorldstate *worldstate = nullptr;

    };
}

#endif //OPENMW_WORLDSTATEPACKET_HPP
