#include "keyboardmanager.hpp"

#include <cctype>

#include <MyGUI_InputManager.h>

#include <components/sdlutil/sdlmappings.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/inputmanager.hpp"
#include "../mwbase/luamanager.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwmp/GUIController.hpp"
#include "../mwmp/Main.hpp"

#include "actions.hpp"
#include "bindingsmanager.hpp"

namespace MWInput
{
    KeyboardManager::KeyboardManager(BindingsManager* bindingsManager)
        : mBindingsManager(bindingsManager)
    {
    }

    void KeyboardManager::textInput(const SDL_TextInputEvent& arg)
    {
        mwmp::GUIController* guiController
            = mwmp::Main::isInitialized() ? mwmp::Main::get().getGUIController() : nullptr;
        const bool chatEditing = guiController && guiController->getChatEditState();

        MyGUI::UString ustring(&arg.text[0]);
        MyGUI::UString::utf32string utf32string = ustring.asUTF32();
        for (MyGUI::UString::utf32string::const_iterator it = utf32string.begin(); it != utf32string.end(); ++it)
        {
            if (chatEditing)
                guiController->injectChatKeyPress(MyGUI::KeyCode::None, *it);
            else
                MyGUI::InputManager::getInstance().injectKeyPress(MyGUI::KeyCode::None, *it);
        }
    }

    void KeyboardManager::keyPressed(const SDL_KeyboardEvent& arg)
    {
        // HACK: to make default keybinding for the console work without printing an extra "^" upon closing
        // This assumes that SDL_TextInput events always come *after* the key event
        // (which is somewhat reasonable, and hopefully true for all SDL platforms)
        auto kc = SDLUtil::sdlKeyToMyGUI(arg.keysym.sym);
        if (mBindingsManager->getKeyBinding(A_Console) == arg.keysym.scancode
            // HACK: allow upper case variant of console keybinding.
            && (arg.keysym.mod & KMOD_SHIFT) == 0 && MWBase::Environment::get().getWindowManager()->isConsoleMode())
            SDL_StopTextInput();

        /*
            Start of mwnet addition

            Handle multiplayer GUI shortcuts before MyGUI gets a chance to consume
            them. The chat overlay is visible during gameplay and OpenMW 0.52 routes
            its keys through MyGUI before the regular bindings manager.
        */
        MWBase::InputManager* input = MWBase::Environment::get().getInputManager();
        mwmp::GUIController* guiController
            = mwmp::Main::isInitialized() ? mwmp::Main::get().getGUIController() : nullptr;
        if (!arg.repeat && guiController && guiController->pressedKey(arg.keysym.scancode))
        {
            mBindingsManager->setPlayerControlsEnabled(!guiController->getChatEditState());
            input->setJoystickLastUsed(false);
            return;
        }
        /* End of mwnet addition */

        const bool chatEditing = guiController && guiController->getChatEditState();
        const bool isFunctionKey
            = arg.keysym.scancode >= SDL_SCANCODE_F1 && arg.keysym.scancode <= SDL_SCANCODE_F12;

        /*
            Start of mwnet addition

            Dispatch chat editing keys only after restoring the chat widget's focus.
            Function keys deliberately bypass this path and retain their normal bindings.
        */
        if (chatEditing && !isFunctionKey)
        {
            if (kc != MyGUI::KeyCode::None && !mBindingsManager->isDetectingBindingState())
                guiController->injectChatKeyPress(kc, 0);

            mBindingsManager->setPlayerControlsEnabled(false);
            input->setJoystickLastUsed(false);
            return;
        }
        /* End of mwnet addition */

        bool consumed = SDL_IsTextInputActive() && // Little trick to check if key is printable
            (!(SDLK_SCANCODE_MASK & arg.keysym.sym)
                && // Don't trust isprint for symbols outside the extended ASCII range
                ((kc == MyGUI::KeyCode::None && arg.keysym.sym > 0xff)
                    || (arg.keysym.sym >= 0 && arg.keysym.sym <= 255 && std::isprint(arg.keysym.sym))));
        if (kc != MyGUI::KeyCode::None && !mBindingsManager->isDetectingBindingState())
        {
            if (MWBase::Environment::get().getWindowManager()->injectKeyPress(kc, 0, arg.repeat))
                consumed = true;
            mBindingsManager->setPlayerControlsEnabled(!consumed && !chatEditing);
        }

        if (arg.repeat)
            return;

        if (!input->controlsDisabled() && !consumed)
            mBindingsManager->keyPressed(arg);

        if (!consumed)
        {
            MWBase::Environment::get().getLuaManager()->inputEvent(
                { MWBase::LuaManager::InputEvent::KeyPressed, arg.keysym });
        }

        input->setJoystickLastUsed(false);
    }

    void KeyboardManager::keyReleased(const SDL_KeyboardEvent& arg)
    {
        MWBase::InputManager* input = MWBase::Environment::get().getInputManager();
        input->setJoystickLastUsed(false);
        auto kc = SDLUtil::sdlKeyToMyGUI(arg.keysym.sym);

        mwmp::GUIController* guiController
            = mwmp::Main::isInitialized() ? mwmp::Main::get().getGUIController() : nullptr;
        const bool chatEditing = guiController && guiController->getChatEditState();
        const bool isFunctionKey
            = arg.keysym.scancode >= SDL_SCANCODE_F1 && arg.keysym.scancode <= SDL_SCANCODE_F12;

        if (chatEditing && !isFunctionKey)
        {
            MyGUI::InputManager::getInstance().injectKeyRelease(kc);
            mBindingsManager->setPlayerControlsEnabled(false);
            return;
        }

        if (!mBindingsManager->isDetectingBindingState())
            mBindingsManager->setPlayerControlsEnabled(
                !MyGUI::InputManager::getInstance().injectKeyRelease(kc) && !chatEditing);
        mBindingsManager->keyReleased(arg);
        MWBase::Environment::get().getLuaManager()->inputEvent(
            { MWBase::LuaManager::InputEvent::KeyReleased, arg.keysym });
    }
}
