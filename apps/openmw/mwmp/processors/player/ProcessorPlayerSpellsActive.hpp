#ifndef OPENMW_PROCESSORPLAYERSPELLSACTIVE_HPP
#define OPENMW_PROCESSORPLAYERSPELLSACTIVE_HPP


#include "../PlayerProcessor.hpp"

namespace mwmp
{
    class ProcessorPlayerSpellsActive final: public PlayerProcessor
    {
    public:
        ProcessorPlayerSpellsActive()
        {
            BPP_INIT(ID_PLAYER_SPELLS_ACTIVE)
        }

        void Do(PlayerPacket &packet, BasePlayer *player) override
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Received ID_PLAYER_SPELLS_ACTIVE from server");

            if (isLocal())
            {
                LOG_APPEND(TimedLog::LOG_INFO, "- Packet was about me");

                if (isRequest())
                    static_cast<LocalPlayer*>(player)->sendSpellsActive();
                else
                {
                    LocalPlayer& localPlayer = static_cast<LocalPlayer&>(*player);

                    int spellsActiveAction = localPlayer.spellsActiveChanges.action;

                    if (spellsActiveAction == SpellsActiveChanges::ADD)
                        localPlayer.addSpellsActive();
                    else if (spellsActiveAction == SpellsActiveChanges::REMOVE)
                        localPlayer.removeSpellsActive();
                    else // SpellsActiveChanges::SET
                        localPlayer.setSpellsActive();
                }
            }
            else if (player != nullptr)
            {
                LOG_APPEND(TimedLog::LOG_INFO, "- Packet was about %s", player->npc.mName.c_str());

                DedicatedPlayer& dedicatedPlayer = static_cast<DedicatedPlayer&>(*player);

                int spellsActiveAction = dedicatedPlayer.spellsActiveChanges.action;

                if (spellsActiveAction == SpellsActiveChanges::ADD)
                    dedicatedPlayer.addSpellsActive();
                else if (spellsActiveAction == SpellsActiveChanges::REMOVE)
                    dedicatedPlayer.removeSpellsActive();
                else // SpellsActiveChanges::SET
                    dedicatedPlayer.setSpellsActive();
            }
        }
    };
}

#endif //OPENMW_PROCESSORPLAYERSPELLSACTIVE_HPP
