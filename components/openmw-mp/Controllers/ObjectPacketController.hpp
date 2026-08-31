#ifndef OPENMW_OBJECTPACKETCONTROLLER_HPP
#define OPENMW_OBJECTPACKETCONTROLLER_HPP


#include "../Packets/Object/ObjectPacket.hpp"
#include <cstdint>
#include <unordered_map>
#include <memory>

namespace mwmp
{
    namespace transport { class ApplicationPacketDispatcher; }
    class ObjectPacketController
    {
    public:
        ObjectPacketController();
        ObjectPacket *GetPacket(std::uint16_t id);
        void SetApplicationPacketDispatcher(transport::ApplicationPacketDispatcher* dispatcher);

        bool ContainsPacket(std::uint16_t id);

        typedef std::unordered_map<unsigned char, std::unique_ptr<ObjectPacket> > packets_t;
    private:
        packets_t packets;
    };
}

#endif //OPENMW_OBJECTPACKETCONTROLLER_HPP
