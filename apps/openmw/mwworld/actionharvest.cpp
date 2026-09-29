#include "actionharvest.hpp"

#include <sstream>

#include <MyGUI_LanguageManager.h>

#include <components/misc/strings/format.hpp>

    /*
    Start of mwnet addition

    Include additional headers for multiplayer purposes
*/
#include "../mwmp/Main.hpp"
#include "../mwmp/Networking.hpp"
#include "../mwmp/LocalPlayer.hpp"
#include "../mwmp/ObjectList.hpp"
/* End of mwnet addition */
#include "../mwbase/environment.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"

#include "../mwrender/animation.hpp"

#include "class.hpp"
#include "containerstore.hpp"

namespace MWWorld
{
    ActionHarvest::ActionHarvest(const MWWorld::Ptr& container)
        : Action(true, container)
    {
        setSound(ESM::RefId::stringRefId("Item Ingredient Up"));
    }

    void ActionHarvest::executeImp(const MWWorld::Ptr& actor)
    {
        if (!MWBase::Environment::get().getWindowManager()->isAllowed(MWGui::GW_Inventory))
            return;

        MWWorld::Ptr target = getTarget();
        MWWorld::ContainerStore& store = target.getClass().getContainerStore(target);

            /*
            Start of mwnet addition

            Prepare an ID_CONTAINER packet that will let the server know about the
            items removed from the harvested objects
        */
        mwmp::ObjectList* objectList = mwmp::Main::get().getNetworking()->getObjectList();
        objectList->reset();
        objectList->packetOrigin = mwmp::CLIENT_GAMEPLAY;
        objectList->cell = target.getCell()->getCell()->getEsm3();
        objectList->action = mwmp::BaseObjectList::REMOVE;
        objectList->containerSubAction = mwmp::BaseObjectList::NONE;

        mwmp::BaseObject baseObject = objectList->getBaseObjectFromPtr(target);
        /* End of mwnet addition */
        store.resolve();
        MWWorld::ContainerStore& actorStore = actor.getClass().getContainerStore(actor);
        std::map<std::string, int> takenMap;
        for (MWWorld::ContainerStoreIterator it = store.begin(); it != store.end(); ++it)
        {
            if (!it->getClass().showsInInventory(*it))
                continue;

            int itemCount = it->getCellRef().getCount();
            // Note: it is important to check for crime before move an item from container. Otherwise owner check will
            // not work for a last item in the container - empty harvested containers are considered as "allowed to
            // use".
            MWBase::Environment::get().getMechanicsManager()->itemTaken(actor, *it, target, itemCount);
            actorStore.add(*it, itemCount);
            store.remove(*it, itemCount);
            std::string name{ it->getClass().getName(*it) };
            takenMap[name] += itemCount;

                /*
                Start of mwnet addition

                Track this item removal in the ID_CONTAINER packet being prepared
            */
            objectList->addContainerItem(baseObject, *it, 0, itemCount);
            /* End of mwnet addition */
        }

        /*
            Start of mwnet addition

            Send an ID_CONTAINER packet if the local player is logged in
        */
        if (mwmp::Main::get().getLocalPlayer()->isLoggedIn())
        {
            objectList->addBaseObject(baseObject);
            objectList->sendContainer();
        }
        /* End of mwnet addition */

        // Spawn a messagebox (only for items added to player's inventory)
        if (actor == MWBase::Environment::get().getWorld()->getPlayerPtr())
        {
            std::ostringstream stream;
            int lineCount = 0;
            const static int maxLines = 10;
            for (const auto& pair : takenMap)
            {
                const std::string& itemName = pair.first;
                int itemCount = pair.second;
                lineCount++;
                if (lineCount == maxLines)
                    stream << "\n...";
                else if (lineCount > maxLines)
                    break;

                // The two GMST entries below expand to strings informing the player of what, and how many of it has
                // been added to their inventory
                std::string msgBox;
                if (itemCount == 1)
                {
                    msgBox = MyGUI::LanguageManager::getInstance().replaceTags("\n#{sNotifyMessage60}");
                    msgBox = Misc::StringUtils::format(msgBox, itemName);
                }
                else
                {
                    msgBox = MyGUI::LanguageManager::getInstance().replaceTags("\n#{sNotifyMessage61}");
                    msgBox = Misc::StringUtils::format(msgBox, itemCount, itemName);
                }

                stream << msgBox;
            }
            std::string tooltip = stream.str();
            // remove the first newline (easier this way)
            if (tooltip.size() > 0 && tooltip[0] == '\n')
                tooltip.erase(0, 1);

            if (tooltip.size() > 0)
                MWBase::Environment::get().getWindowManager()->messageBox(tooltip);
        }

        auto world = MWBase::Environment::get().getWorld();
        MWRender::Animation* anim = world->getAnimation(target);
        if (anim != nullptr)
            anim->harvest(target);
    }
}
