#ifndef PLUGINSYSTEM3_SCRIPT_HPP
#define PLUGINSYSTEM3_SCRIPT_HPP

#include <boost/any.hpp>
#include <exception>
#include <unordered_map>
#include <memory>

#include "Types.hpp"
#include "SystemInterface.hpp"
#include "ScriptFunction.hpp"
#include "ScriptFunctions.hpp"
#include "Language.hpp"

#include "Networking.hpp"

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

    class CallbackContext
    {
    public:
        explicit CallbackContext(bool preAuthentication) noexcept;
        ~CallbackContext();

        CallbackContext(const CallbackContext&) = delete;
        CallbackContext& operator=(const CallbackContext&) = delete;

    private:
        bool mPreAuthentication = false;
    };

    static thread_local unsigned int sPreAuthenticationDepth;

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

        CallbackContext context(I == CallbackIdentity("OnTransportConnect"));
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
};

#endif //PLUGINSYSTEM3_SCRIPT_HPP
