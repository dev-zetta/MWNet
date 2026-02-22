#ifndef OPENMW_PROCESSORPLAYERALLY_HPP
#define OPENMW_PROCESSORPLAYERALLY_HPP

#include "../PlayerProcessor.hpp"
#include "apps/openmw/mwmp/Main.hpp"
#include "apps/openmw/mwmp/LocalPlayer.hpp"

namespace mwmp
{
    class ProcessorPlayerAlly final: public PlayerProcessor
    {
    public:
        ProcessorPlayerAlly()
        {
            BPP_INIT(ID_PLAYER_ALLY)
        }

        void Do(PlayerPacket &packet, BasePlayer *player) override
        {
            mwmp::LocalPlayer *localPlayer = mwmp::Main::get().getLocalPlayer();

            if (isLocal())
            {
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Received ID_PLAYER_ALLY about LocalPlayer %s from server", localPlayer->npc.mName.c_str());

                for (const auto& guid : localPlayer->alliedPlayers)
                {
                    DedicatedPlayer *dedicatedPlayer = PlayerList::getPlayer(guid);

                    if (dedicatedPlayer)
                    {
                        LOG_APPEND(TimedLog::LOG_INFO, "- Adding DedicatedPlayer %s to our allied players", dedicatedPlayer->npc.mName.c_str());
                    }
                }
            }
            else if (player != nullptr)
            {
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Received ID_PLAYER_ALLY about DedicatedPlayer %s from server", player->npc.mName.c_str());

                for (const auto& alliedGuid : player->alliedPlayers)
                {
                    if (alliedGuid == localPlayer->guid)
                    {
                        LOG_APPEND(TimedLog::LOG_INFO, "- Adding LocalPlayer %s to their allied players", localPlayer->npc.mName.c_str());
                    }
                    else
                    {
                        DedicatedPlayer *otherDedicatedPlayer = PlayerList::getPlayer(alliedGuid);

                        if (otherDedicatedPlayer)
                        {
                            LOG_APPEND(TimedLog::LOG_INFO, "- Adding DedicatedPlayer %s to their allied players", otherDedicatedPlayer->npc.mName.c_str());
                        }
                    }
                }
            }
        }
    };
}

#endif //OPENMW_PROCESSORPLAYERALLY_HPP
