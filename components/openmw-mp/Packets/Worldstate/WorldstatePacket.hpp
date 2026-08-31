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
        bool beginDecodeTransaction() override;
        void commitDecodeTransaction() noexcept override;
        void rollbackDecodeTransaction() noexcept override;

        BaseWorldstate *worldstate = nullptr;
        protocol::DecodeTransaction<BaseWorldstate> mDecodeTransaction;

    };
}

#endif //OPENMW_WORLDSTATEPACKET_HPP
