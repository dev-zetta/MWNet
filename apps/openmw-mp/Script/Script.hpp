#ifndef PLUGINSYSTEM3_SCRIPT_HPP
#define PLUGINSYSTEM3_SCRIPT_HPP

#include <boost/any.hpp>
#include <exception>
#include <unordered_map>
#include <memory>
#include <optional>

#include "Types.hpp"
#include "SystemInterface.hpp"
#include "ScriptFunction.hpp"
#include "ScriptFunctions.hpp"
#include "Language.hpp"

#include "Networking.hpp"

#include <components/openmw-mp/Script/LuaApiPolicy.hpp>

class Script : private ScriptFunctions
{
    // http://imgur.com/hU0N4EH
private:

    std::unique_ptr<Language> lang;

    enum
    {
        SCRIPT_CPP,
        SCRIPT_LUA
    };

    template<typename R>
    R GetScript(const char *name)
    {
        if (script_type == SCRIPT_CPP)
        {
            return SystemInterface<R>(lang->GetInterface(), name).result;
        }
        else
        {
            return reinterpret_cast<R>(lang->IsCallbackPresent(name));
        }
    }

    int script_type = -1;
    std::unordered_map<unsigned int, FunctionEllipsis<void>> callbacks_;
    std::unordered_map<unsigned int, FunctionEllipsis<bool>> booleanCallbacks_;

    class CallbackContext
    {
    public:
        CallbackContext(bool preAuthentication,
            std::optional<unsigned short> intentPlayer) noexcept;
        ~CallbackContext();

        CallbackContext(const CallbackContext&) = delete;
        CallbackContext& operator=(const CallbackContext&) = delete;

    private:
        bool mPreAuthentication = false;
        bool mIntentValidation = false;
        std::optional<unsigned short> mPreviousIntentPlayer;
    };

    static thread_local unsigned int sPreAuthenticationDepth;
    static thread_local unsigned int sIntentValidationDepth;
    static thread_local std::optional<unsigned short> sIntentPlayer;

    static std::optional<unsigned short> CallbackPlayer() noexcept
    {
        return std::nullopt;
    }

    template<typename First, typename... Rest>
    static std::optional<unsigned short> CallbackPlayer(
        First&& first, Rest&&...) noexcept
    {
        if constexpr (std::is_convertible_v<std::remove_reference_t<First>,
                          unsigned short>)
            return static_cast<unsigned short>(first);
        return std::nullopt;
    }

    using ScriptList = std::vector<std::unique_ptr<Script>>;
    static ScriptList scripts;

    Script(const char *path);

    Script(const Script&) = delete;
    Script& operator=(const Script&) = delete;

protected:
    static std::string moddir;
public:
    ~Script() = default;

    static void LoadScript(const char *script, const char* base);
    static void LoadScripts(char* scripts, const char* base);
    static void UnloadScripts();
    static void SetModDir(const std::string &moddir);
    static const char* GetModDir();
    static bool IsPreAuthenticationCallback() noexcept;
    static bool IsIntentValidationCallback() noexcept;
    static std::optional<unsigned short> GetIntentPlayer() noexcept;

    static constexpr ScriptCallbackData const& CallBackData(const unsigned int I, const unsigned int N = 0) {
        return callbacks[N].index == I ? callbacks[N] : CallBackData(I, N + 1);
    }

    template<size_t N>
    static constexpr unsigned int CallbackIdentity(const char(&str)[N])
    {
        return Utils::hash(str);
    }

    template<unsigned int I, bool B = false, typename... Args>
    static unsigned int Call(Args&&... args) {
        constexpr ScriptCallbackData const& data = CallBackData(I);
        static_assert(data.callback.matches(TypeString<typename std::remove_reference<Args>::type...>::value),
                      "Wrong number or types of arguments");

        CallbackContext context(I == CallbackIdentity("OnTransportConnect"),
            std::nullopt);
        unsigned int count = 0;

        for (auto& script : scripts)
        {
            if (!script->callbacks_.count(I))
                script->callbacks_.emplace(I, script->GetScript<FunctionEllipsis<void>>(data.name));

            auto callback = script->callbacks_[I];

            if (!callback)
                continue;

            if (script->script_type == SCRIPT_CPP)
                (callback)(std::forward<Args>(args)...);
#if defined (ENABLE_LUA)
            else if (script->script_type == SCRIPT_LUA)
            {
                try
                {
                    script->lang->Call(data.name, data.callback.types, B, std::forward<Args>(args)...);
                }
                catch (std::exception &e)
                {
                    LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "%s", e.what());
                    if constexpr (I != CallbackIdentity("OnServerScriptCrash"))
                    {
                        try
                        {
                            Script::Call<Script::CallbackIdentity("OnServerScriptCrash")>(e.what());
                        }
                        catch (const std::exception& crashException)
                        {
                            LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR,
                                "OnServerScriptCrash failed: %s", crashException.what());
                        }
                        catch (...)
                        {
                            LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR,
                                "%s", "OnServerScriptCrash failed with an unknown exception");
                        }
                    }

                    if (!mwmp::Networking::getPtr()->getScriptErrorIgnoringState())
                        throw;
                }
            }
#endif
            ++count;
        }

        return count;
    }

    template<unsigned int I, typename... Args>
    static bool CallBoolean(Args&&... args)
    {
        constexpr ScriptCallbackData const& data = CallBackData(I);
        static_assert(data.callback.matches(TypeString<typename std::remove_reference<Args>::type...>::value),
            "Wrong number or types of arguments");

        CallbackContext context(false, mwmp::script::isIntentCallback(data.name)
                ? CallbackPlayer(args...) : std::nullopt);
        bool allowed = true;
        for (auto& script : scripts)
        {
            try
            {
                if (script->script_type == SCRIPT_CPP)
                {
                    if (!script->booleanCallbacks_.count(I))
                    {
                        script->booleanCallbacks_.emplace(I,
                            script->GetScript<FunctionEllipsis<bool>>(data.name));
                    }
                    auto callback = script->booleanCallbacks_[I];
                    if (callback && !(callback)(std::forward<Args>(args)...))
                        allowed = false;
                }
#if defined (ENABLE_LUA)
                else if (script->script_type == SCRIPT_LUA
                    && script->lang->IsCallbackPresent(data.name))
                {
                    const boost::any result = script->lang->Call(data.name,
                        data.callback.types, 0, std::forward<Args>(args)...);
                    if (!result.empty())
                    {
                        if (result.type() != typeid(bool))
                            throw std::runtime_error(std::string(data.name)
                                + " must return a boolean or nil");
                        if (!boost::any_cast<bool>(result))
                            allowed = false;
                    }
                }
#endif
            }
            catch (const std::exception& exception)
            {
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "%s", exception.what());
                if constexpr (I != CallbackIdentity("OnServerScriptCrash"))
                {
                    try
                    {
                        Script::Call<Script::CallbackIdentity("OnServerScriptCrash")>(
                            exception.what());
                    }
                    catch (const std::exception& crashException)
                    {
                        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR,
                            "OnServerScriptCrash failed: %s", crashException.what());
                    }
                    catch (...)
                    {
                        LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR,
                            "%s", "OnServerScriptCrash failed with an unknown exception");
                    }
                }
                if (!mwmp::Networking::getPtr()->getScriptErrorIgnoringState())
                    throw;
                allowed = false;
            }
            catch (...)
            {
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_ERROR, "%s",
                    "Unknown exception in boolean script callback");
                if (!mwmp::Networking::getPtr()->getScriptErrorIgnoringState())
                    throw;
                allowed = false;
            }
        }
        return allowed;
    }
};

#endif //PLUGINSYSTEM3_SCRIPT_HPP
