#ifndef OPENMW_PLAYERPACKET_HPP
#define OPENMW_PLAYERPACKET_HPP

#include <string>
#include <components/openmw-mp/Base/BasePlayer.hpp>

#include <components/openmw-mp/Packets/BasePacket.hpp>

namespace mwmp
{
    class PlayerPacket : public BasePacket
    {
    public:
        PlayerPacket();

        ~PlayerPacket();

        void setPlayer(BasePlayer *newPlayer);
        BasePlayer *getPlayer();

    protected:
        bool beginDecodeTransaction() override;
        void commitDecodeTransaction() noexcept override;
        void rollbackDecodeTransaction() noexcept override;

        BasePlayer *player = nullptr;
        protocol::DecodeTransaction<BasePlayer> mDecodeTransaction;

    };
}

#endif //OPENMW_PLAYERPACKET_HPP
