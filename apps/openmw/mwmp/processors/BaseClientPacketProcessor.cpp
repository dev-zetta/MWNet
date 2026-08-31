#include "BaseClientPacketProcessor.hpp"
#include "../Main.hpp"

using namespace mwmp;

mwmp::transport::TransportConnectionId BaseClientPacketProcessor::guid;
mwmp::transport::TransportConnectionId BaseClientPacketProcessor::myGuid;
bool BaseClientPacketProcessor::request;

LocalPlayer *BaseClientPacketProcessor::getLocalPlayer()
{
    return Main::get().getLocalPlayer();
}
