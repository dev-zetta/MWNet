#ifndef OPENMW_OBJECTPACKET_HPP
#define OPENMW_OBJECTPACKET_HPP

#include <string>
#include <components/openmw-mp/Base/BaseObject.hpp>

#include <components/openmw-mp/Packets/BasePacket.hpp>


namespace mwmp
{
    class ObjectPacket : public BasePacket
    {
    public:
        ObjectPacket();

        ~ObjectPacket();

        void setObjectList(BaseObjectList *newObjectList);

        virtual void Packet(bool send);

    protected:
        bool beginDecodeTransaction() override;
        void commitDecodeTransaction() noexcept override;
        void rollbackDecodeTransaction() noexcept override;

        virtual void Object(BaseObject &baseObject, bool send);
        bool PacketHeader(bool send);
        BaseObjectList *objectList = nullptr;
        protocol::DecodeTransaction<BaseObjectList> mDecodeTransaction;
        static const int maxObjects = 3000;
        bool hasCellData = false;
    };
}

#endif //OPENMW_OBJECTPACKET_HPP
