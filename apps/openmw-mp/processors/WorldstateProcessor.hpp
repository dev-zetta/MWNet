#ifndef OPENMW_BASEWORLDSTATEPROCESSOR_HPP
#define OPENMW_BASEWORLDSTATEPROCESSOR_HPP

#include <components/openmw-mp/Base/BasePacketProcessor.hpp>
#include <components/openmw-mp/Packets/BasePacket.hpp>
#include <components/openmw-mp/Packets/Worldstate/WorldstatePacket.hpp>
#include <components/openmw-mp/Transport/ApplicationPacketReceiver.hpp>
#include <components/openmw-mp/NetworkMessages.hpp>
#include "Player.hpp"

namespace mwmp
{
    class WorldstateProcessor : public BasePacketProcessor<WorldstateProcessor>
    {
    public:
        virtual ~WorldstateProcessor() = default;

        virtual void Do(WorldstatePacket &packet, Player &player, BaseWorldstate &worldstate);
        virtual bool Validate(Player&, const BaseWorldstate&) { return true; }

        static bool Process(const mwmp::transport::ReceivedApplicationPacket& packet, BaseWorldstate &worldstate);
    };
}

#endif //OPENMW_BASEWORLDSTATEPROCESSOR_HPP
