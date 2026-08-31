#ifndef OPENMW_PACKETRECORDDYNAMIC_HPP
#define OPENMW_PACKETRECORDDYNAMIC_HPP

#include <components/openmw-mp/Packets/Worldstate/WorldstatePacket.hpp>
#include <components/openmw-mp/NetworkMessages.hpp>

namespace mwmp
{
    class PacketRecordDynamic: public WorldstatePacket
    {
    public:
        PacketRecordDynamic();

        virtual void Packet(bool send);

        void ProcessEffects(ESM::EffectList &effectList, bool send);
        void ProcessBodyParts(ESM::PartReferenceList &bodyPartList, bool send);
        void ProcessInventoryList(std::vector<mwmp::Item> &inventory, ESM::InventoryList &inventoryList, bool send);

    protected:
        static inline constexpr int maxRecords = 3000;
        static inline constexpr int maxEffects = 100;
        static inline constexpr int maxParts = 7;
        static inline constexpr int maxItems = 1000;
    };
}

#endif //OPENMW_PACKETRECORDDYNAMIC_HPP
