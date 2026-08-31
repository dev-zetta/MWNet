#ifndef OPENMW_SYSTEMPACKET_HPP
#define OPENMW_SYSTEMPACKET_HPP

#include <string>
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
        bool beginDecodeTransaction() override;
        void commitDecodeTransaction() noexcept override;
        void rollbackDecodeTransaction() noexcept override;

        BaseSystem *system = nullptr;
        protocol::DecodeTransaction<BaseSystem> mDecodeTransaction;

    };
}

#endif //OPENMW_SYSTEMPACKET_HPP
