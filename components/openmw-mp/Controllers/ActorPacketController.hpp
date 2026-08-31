#ifndef OPENMW_ACTORPACKETCONTROLLER_HPP
#define OPENMW_ACTORPACKETCONTROLLER_HPP


#include "../Packets/Actor/ActorPacket.hpp"
#include <cstdint>
#include <unordered_map>
#include <memory>

namespace mwmp
{
    namespace transport { class ApplicationPacketDispatcher; }
    class ActorPacketController
    {
    public:
        ActorPacketController();
        ActorPacket *GetPacket(std::uint16_t id);
        void SetStream(RakNet::BitStream *inStream, RakNet::BitStream *outStream);
        void SetApplicationPacketDispatcher(transport::ApplicationPacketDispatcher* dispatcher);

        bool ContainsPacket(std::uint16_t id);

        typedef std::unordered_map<unsigned char, std::unique_ptr<ActorPacket> > packets_t;
    private:
        packets_t packets;
    };
}

#endif //OPENMW_ACTORPACKETCONTROLLER_HPP
