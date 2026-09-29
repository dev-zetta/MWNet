#include "actiontake.hpp"

/*
    Start of mwnet addition

    Include additional headers for multiplayer purposes
*/
#include "../mwmp/Main.hpp"
#include "../mwmp/Networking.hpp"
#include "../mwmp/ObjectList.hpp"
/* End of mwnet addition */

#include "../mwbase/environment.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"

#include "../mwgui/inventorywindow.hpp"

#include "class.hpp"
#include "containerstore.hpp"

namespace MWWorld
{
    ActionTake::ActionTake(const MWWorld::Ptr& object)
        : Action(true, object)
    {
    }

    void ActionTake::executeImp(const Ptr& actor)
    {
        // When in GUI mode, we should use drag and drop
        if (actor == MWBase::Environment::get().getWorld()->getPlayerPtr())
        {
            MWGui::GuiMode mode = MWBase::Environment::get().getWindowManager()->getMode();
            if (mode == MWGui::GM_Inventory || mode == MWGui::GM_Container)
            {
                MWBase::Environment::get().getWindowManager()->getInventoryWindow()->pickUpObject(getTarget());
                return;
            }
        }

        int count = getTarget().getCellRef().getCount();
        if (getTarget().getClass().isGold(getTarget()))
            count *= getTarget().getClass().getValue(getTarget());

        MWBase::Environment::get().getMechanicsManager()->itemTaken(actor, getTarget(), MWWorld::Ptr(), count);
        MWWorld::Ptr newitem = *actor.getClass().getContainerStore(actor).add(getTarget(), count);
        MWBase::Environment::get().getWorld()->deleteObject(getTarget());

            /*
            Start of mwnet addition

            Send an ID_OBJECT_DELETE packet every time an item is taken from the world
            by the player outside of the inventory screen
        */
        mwmp::ObjectList *objectList = mwmp::Main::get().getNetworking()->getObjectList();
        objectList->reset();
        objectList->packetOrigin = mwmp::CLIENT_GAMEPLAY;
        objectList->addObjectGeneric(getTarget());
        objectList->sendObjectDelete();
        /* End of mwnet addition */
        setTarget(newitem);
    }
}
