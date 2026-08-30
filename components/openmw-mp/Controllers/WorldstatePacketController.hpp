#ifndef OPENMW_WORLDSTATEPACKETCONTROLLER_HPP
#define OPENMW_WORLDSTATEPACKETCONTROLLER_HPP


#include <RakPeerInterface.h>
#include "../Packets/Worldstate/WorldstatePacket.hpp"
#include <unordered_map>
#include <memory>

namespace mwmp
{
    namespace transport { class ApplicationPacketDispatcher; }
    class WorldstatePacketController
    {
    public:
        WorldstatePacketController(RakNet::RakPeerInterface *peer);
        WorldstatePacket *GetPacket(RakNet::MessageID id);
        void SetStream(RakNet::BitStream *inStream, RakNet::BitStream *outStream);
        void SetApplicationPacketDispatcher(transport::ApplicationPacketDispatcher* dispatcher);

        bool ContainsPacket(RakNet::MessageID id);

        typedef std::unordered_map<unsigned char, std::unique_ptr<WorldstatePacket> > packets_t;
    private:
        packets_t packets;
    };
}

#endif //OPENMW_WORLDSTATEPACKETCONTROLLER_HPP
