#ifndef OPENMW_SYSTEMPACKETCONTROLLER_HPP
#define OPENMW_SYSTEMPACKETCONTROLLER_HPP


#include "../Packets/System/SystemPacket.hpp"
#include <cstdint>
#include <unordered_map>
#include <memory>

namespace mwmp
{
    namespace transport { class ApplicationPacketDispatcher; }
    class SystemPacketController
    {
    public:
        SystemPacketController();
        SystemPacket *GetPacket(std::uint16_t id);
        void SetApplicationPacketDispatcher(transport::ApplicationPacketDispatcher* dispatcher);

        bool ContainsPacket(std::uint16_t id);

        typedef std::unordered_map<unsigned char, std::unique_ptr<SystemPacket> > packets_t;
    private:
        packets_t packets;
    };
}

#endif //OPENMW_SYSTEMPACKETCONTROLLER_HPP
