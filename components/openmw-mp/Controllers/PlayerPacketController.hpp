#ifndef OPENMW_PLAYERPACKETCONTROLLER_HPP
#define OPENMW_PLAYERPACKETCONTROLLER_HPP


#include "../Packets/Player/PlayerPacket.hpp"
#include <cstdint>
#include <unordered_map>
#include <memory>

namespace mwmp
{
    namespace transport { class ApplicationPacketDispatcher; }
    class PlayerPacketController
    {
    public:
        PlayerPacketController();
        PlayerPacket *GetPacket(std::uint16_t id);
        void SetStream(RakNet::BitStream *inStream, RakNet::BitStream *outStream);
        void SetApplicationPacketDispatcher(transport::ApplicationPacketDispatcher* dispatcher);

        bool ContainsPacket(std::uint16_t id);

        typedef std::unordered_map<unsigned char, std::unique_ptr<PlayerPacket> > packets_t;
    private:
        packets_t packets;
    };
}

#endif //OPENMW_PLAYERPACKETCONTROLLER_HPP
