#ifndef OPENMW_SYSTEMPACKETCONTROLLER_HPP
#define OPENMW_SYSTEMPACKETCONTROLLER_HPP


#include "../Packets/System/SystemPacket.hpp"
#include <unordered_map>
#include <memory>

namespace mwmp
{
    namespace transport { class ApplicationPacketDispatcher; }
    class SystemPacketController
    {
    public:
        SystemPacketController();
        SystemPacket *GetPacket(RakNet::MessageID id);
        void SetStream(RakNet::BitStream *inStream, RakNet::BitStream *outStream);
        void SetApplicationPacketDispatcher(transport::ApplicationPacketDispatcher* dispatcher);

        bool ContainsPacket(RakNet::MessageID id);

        typedef std::unordered_map<unsigned char, std::unique_ptr<SystemPacket> > packets_t;
    private:
        packets_t packets;
    };
}

#endif //OPENMW_SYSTEMPACKETCONTROLLER_HPP
