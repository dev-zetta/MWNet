#include <cstdio>
#include <exception>
#include <iostream>
#include <string_view>
#include "LangLua.hpp"
#include <Script/Script.hpp>
#include <Script/Types.hpp>

#include <components/openmw-mp/Script/LuaApiPolicy.hpp>

std::set<std::string> LangLua::packagePath;
std::set<std::string> LangLua::packageCPath;

namespace
{
    bool allowedBeforeAuthentication(std::string_view name) noexcept
    {
        return name == "LogMessage" || name == "LogAppend" || name == "GetIP"
            || name == "GetMillisecondsSinceServerStart"
            || name == "GetOperatingSystemType" || name == "GetArchitectureType"
            || name == "GetServerVersion" || name == "GetProtocolVersion"
            || name == "GetMaxPlayers" || name == "GetPort" || name == "HasPassword"
            || name == "GetDataFileEnforcementState"
            || name == "GetScriptErrorIgnoringState";
    }

    int raiseLuaApiError(lua_State* lua, const char* message)
    {
        return luaL_error(lua, "TES3MP API error: %s", message);
    }

    bool deniedDuringIntentValidation(std::string_view name) noexcept
    {
        return Script::IsIntentValidationCallback()
            && !mwmp::script::isReadOnlyApi(name)
            && !mwmp::script::isIntentModifierApi(name);
    }

    bool modifiesAnotherPlayer(lua_State* lua, std::string_view name) noexcept
    {
        if (!Script::IsIntentValidationCallback()
            || !mwmp::script::intentModifierUsesPlayerId(name))
        {
            return false;
        }
        const auto intentPlayer = Script::GetIntentPlayer();
        return !intentPlayer || !lua_isnumber(lua, 1)
            || lua_tointeger(lua, 1) != *intentPlayer;
    }

    template<int (*Function)(lua_State*)>
    int safeLuaFunction(lua_State* lua)
    {
        char error[512]{};
        if (Script::IsPreAuthenticationCallback())
            return raiseLuaApiError(lua,
                "this API is unavailable during OnTransportConnect");
        if (deniedDuringIntentValidation("legacy utility"))
            return raiseLuaApiError(lua,
                "legacy utility APIs are unavailable while validating an intent");
        try
        {
            return Function(lua);
        }
        catch (const std::exception& exception)
        {
            std::snprintf(error, sizeof(error), "%s", exception.what());
        }
        catch (...)
        {
            std::snprintf(error, sizeof(error), "%s", "unknown C++ exception");
        }
        return raiseLuaApiError(lua, error);
    }
}

void setLuaPath(lua_State* L, const char* path, bool cpath = false)
{
    std::string field = cpath ? "cpath" : "path";
    lua_getglobal(L, "package");

    lua_getfield(L, -1, field.c_str());
    std::string cur_path = lua_tostring(L, -1);
    cur_path.append(";");
    cur_path.append(path);
    lua_pop(L, 1);
    lua_pushstring(L, cur_path.c_str());
    lua_setfield(L, -2, field.c_str());
    lua_pop(L, 1);
}

lib_t LangLua::GetInterface()
{
    return reinterpret_cast<lib_t>(lua);
}

LangLua::LangLua(lua_State *lua)
    : lua(lua)
{
}

LangLua::LangLua()
{
    lua = luaL_newstate();
    if (!lua)
        throw std::runtime_error("Failed to create Lua state");
    ownsLua = true;
    try
    {
        luaL_openlibs(lua); // load all lua std libs

        std::string p, cp;
        for (auto& path : packagePath)
            p += path + ';';

        for (auto& path : packageCPath)
            cp += path + ';';

        setLuaPath(lua, p.c_str());
        setLuaPath(lua, cp.c_str(), true);
    }
    catch (...)
    {
        FreeProgram();
        throw;
    }

}

LangLua::~LangLua()
{
    FreeProgram();
}

// LuaFunctionDispatcher template struct for Lua function dispatch
template <unsigned int ArgIndex, unsigned int FunctionIndex>
struct LuaFunctionDispatcher {
    // Dispatch Lua function with the given arguments
    template <typename ReturnType, typename... Args>
    inline static ReturnType Dispatch(lua_State*&& lua, Args&&... args) {
        // Retrieve function data
        constexpr ScriptFunctionMetadata const& functionData = ScriptFunctions::functions[FunctionIndex];
        // Retrieve argument from the Lua stack
        auto argument = sol::stack::get<typename CharType<functionData.func.types[ArgIndex - 1]>::type>(lua, ArgIndex);
        // Recursively dispatch the Lua function
        return LuaFunctionDispatcher<ArgIndex - 1, FunctionIndex>::template Dispatch<ReturnType>(
            std::forward<lua_State*>(lua), argument, std::forward<Args>(args)...);
    }
};

// Specialization for LuaFunctionDispatcher when ArgIndex is 0
template <unsigned int FunctionIndex>
struct LuaFunctionDispatcher<0, FunctionIndex> {
    // Dispatch Lua function with the given arguments
    template <typename ReturnType, typename... Args>
    inline static ReturnType Dispatch(lua_State*&&, Args&&... args) {
        // Retrieve function data
        const ScriptFunctionData& functionData = ScriptFunctions::runtimeFunctions[FunctionIndex];
        // Call the C++ function using reinterpret_cast
        return reinterpret_cast<FunctionEllipsis<ReturnType>>(functionData.func.voidAddr())(std::forward<Args>(args)...);
    }
};

// Lua function wrapper for functions returning 'void'
template <unsigned int FunctionIndex>
static typename std::enable_if<ScriptFunctions::functions[FunctionIndex].func.ret == 'v', int>::type LuaFunctionWrapper(lua_State* lua) {
    char error[512]{};
    if (Script::IsPreAuthenticationCallback()
        && !allowedBeforeAuthentication(ScriptFunctions::functions[FunctionIndex].name))
    {
        std::snprintf(error, sizeof(error),
            "%s is unavailable during OnTransportConnect",
            ScriptFunctions::functions[FunctionIndex].name);
        return raiseLuaApiError(lua, error);
    }
    if (deniedDuringIntentValidation(
            ScriptFunctions::functions[FunctionIndex].name))
    {
        std::snprintf(error, sizeof(error),
            "%s cannot mutate canonical state while validating an intent",
            ScriptFunctions::functions[FunctionIndex].name);
        return raiseLuaApiError(lua, error);
    }
    if (modifiesAnotherPlayer(lua,
            ScriptFunctions::functions[FunctionIndex].name))
    {
        std::snprintf(error, sizeof(error),
            "%s cannot modify another player while validating an intent",
            ScriptFunctions::functions[FunctionIndex].name);
        return raiseLuaApiError(lua, error);
    }
    try
    {
        LuaFunctionDispatcher<ScriptFunctions::functions[FunctionIndex].func.numargs,
            FunctionIndex>::template Dispatch<void>(std::forward<lua_State*>(lua));
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::snprintf(error, sizeof(error), "%s", exception.what());
    }
    catch (...)
    {
        std::snprintf(error, sizeof(error), "%s", "unknown C++ exception");
    }
    return raiseLuaApiError(lua, error);
}

// Lua function wrapper for functions with non-void return types
template <unsigned int FunctionIndex>
static typename std::enable_if<ScriptFunctions::functions[FunctionIndex].func.ret != 'v', int>::type LuaFunctionWrapper(lua_State* lua) {
    char error[512]{};
    if (Script::IsPreAuthenticationCallback()
        && !allowedBeforeAuthentication(ScriptFunctions::functions[FunctionIndex].name))
    {
        std::snprintf(error, sizeof(error),
            "%s is unavailable during OnTransportConnect",
            ScriptFunctions::functions[FunctionIndex].name);
        return raiseLuaApiError(lua, error);
    }
    if (deniedDuringIntentValidation(
            ScriptFunctions::functions[FunctionIndex].name))
    {
        std::snprintf(error, sizeof(error),
            "%s cannot mutate canonical state while validating an intent",
            ScriptFunctions::functions[FunctionIndex].name);
        return raiseLuaApiError(lua, error);
    }
    if (modifiesAnotherPlayer(lua,
            ScriptFunctions::functions[FunctionIndex].name))
    {
        std::snprintf(error, sizeof(error),
            "%s cannot modify another player while validating an intent",
            ScriptFunctions::functions[FunctionIndex].name);
        return raiseLuaApiError(lua, error);
    }
    try
    {
        auto result = LuaFunctionDispatcher<ScriptFunctions::functions[FunctionIndex].func.numargs,
            FunctionIndex>::template Dispatch<typename CharType<
                ScriptFunctions::functions[FunctionIndex].func.ret>::type>(
                    std::forward<lua_State*>(lua));
        sol::stack::push(lua, result);
        return 1;
    }
    catch (const std::exception& exception)
    {
        std::snprintf(error, sizeof(error), "%s", exception.what());
    }
    catch (...)
    {
        std::snprintf(error, sizeof(error), "%s", "unknown C++ exception");
    }
    return raiseLuaApiError(lua, error);
}

// Struct for defining Lua functions with names and wrappers
template <unsigned int FunctionIndex>
struct LuaFunctionDefinition {
    static constexpr LuaFunctionData FunctionInfo{
       ScriptFunctions::functions[FunctionIndex].name, LuaFunctionWrapper<FunctionIndex>
    };
};

template<> struct LuaFunctionDefinition<0> { static constexpr LuaFunctionData FunctionInfo{"CreateTimer", safeLuaFunction<&LangLua::CreateTimer>}; };
template<> struct LuaFunctionDefinition<1> { static constexpr LuaFunctionData FunctionInfo{"CreateTimerEx", safeLuaFunction<&LangLua::CreateTimerEx>}; };
template<> struct LuaFunctionDefinition<2> { static constexpr LuaFunctionData FunctionInfo{"MakePublic", safeLuaFunction<&LangLua::MakePublic>}; };
template<> struct LuaFunctionDefinition<3> { static constexpr LuaFunctionData FunctionInfo{"CallPublic", safeLuaFunction<&LangLua::CallPublic>}; };


#ifdef __arm__
template<std::size_t... Is>
struct indices {};
template<std::size_t N, std::size_t... Is>
struct build_indices : build_indices<N-1, N-1, Is...> {};
template<std::size_t... Is>
struct build_indices<0, Is...> : indices<Is...> {};
template<std::size_t N>
using IndicesFor = build_indices<N>;

template<size_t... Indices>
LuaFuctionData *functions(indices<Indices...>)
{

    static LuaFuctionData functions_[sizeof...(Indices)]{
            F_<Indices>::F...
    };

    static_assert(
            sizeof(functions_) / sizeof(functions_[0]) ==
            sizeof(ScriptFunctions::functions) / sizeof(ScriptFunctions::functions[0]),
            "Not all functions have been mapped to Lua");

    return functions_;
}
#else
template<unsigned int I>
struct LuaFunctionInitializer
{
    constexpr static void Initialize(LuaFunctionData *functions_)
    {
        functions_[I] = LuaFunctionDefinition<I>::FunctionInfo;
        LuaFunctionInitializer<I - 1>::Initialize(functions_);
    }
};

template<>
struct LuaFunctionInitializer<0>
{
    constexpr static void Initialize(LuaFunctionData *functions_)
    {
        functions_[0] = LuaFunctionDefinition<0>::FunctionInfo;
    }
};

template<size_t LastI>
LuaFunctionData *GetLuaFunctions()
{
    static LuaFunctionData functions_[LastI];
    LuaFunctionInitializer<LastI - 1>::Initialize(functions_);

    static_assert(
        sizeof(functions_) / sizeof(functions_[0]) ==
        sizeof(ScriptFunctions::functions) / sizeof(ScriptFunctions::functions[0]),
        "Not all functions have been mapped to Lua");

    return functions_;
}
#endif

void LangLua::LoadProgram(const char *filename)
{
    int err = 0;

    if ((err =luaL_loadfile(lua, filename)) != 0)
        throw std::runtime_error("Lua script " + std::string(filename) + " error (" + std::to_string(err) + "): \"" +
                            std::string(lua_tostring(lua, -1)) + "\"");

    constexpr auto functions_n = sizeof(ScriptFunctions::functions) / sizeof(ScriptFunctions::functions[0]);

#ifdef __arm__
    LuaFunctionData *functions_ = GetLuaFunctions(IndicesFor<functions_n>{});
#else
    LuaFunctionData *functions_ = GetLuaFunctions<sizeof(ScriptFunctions::functions) / sizeof(ScriptFunctions::functions[0])>();
#endif
    sol::state_view solLua(lua);
    sol::table tes3mp = solLua.create_named_table("tes3mp");
    for (unsigned i = 0; i < functions_n; i++)
        tes3mp.set_function(functions_[i].name, functions_[i].func);

if ((err = lua_pcall(lua, 0, 0, 0)) != 0) // Run once script for load in memory.
    throw std::runtime_error("Lua script " + std::string(filename) + " error (" + std::to_string(err) + "): \"" +
                        std::string(lua_tostring(lua, -1)) + "\"");

}

int LangLua::FreeProgram()
{
    if (ownsLua && lua)
        lua_close(lua);
    lua = nullptr;
    ownsLua = false;
    return 0;
}

bool LangLua::IsCallbackPresent(const char *name)
{
    return sol::state_view(lua)[name].get_type() == sol::type::function;
}

boost::any LangLua::Call(const char *name, const char *argl, int buf, ...)
{
    va_list vargs;
    va_start(vargs, buf);

    int n_args = (int)(strlen(argl));

    lua_getglobal(lua, name);

    for (int index = 0; index < n_args; index++)
    {
        switch (argl[index])
        {
            case 'i':
                sol::stack::push(lua, va_arg(vargs, unsigned int));
                break;

            case 'q':
                sol::stack::push(lua, va_arg(vargs, signed int));
                break;

            case 'l':
                sol::stack::push(lua, va_arg(vargs, unsigned long long));
                break;

            case 'w':
                sol::stack::push(lua, va_arg(vargs, signed long long));
                break;

            case 'f':
                sol::stack::push(lua, va_arg(vargs, double));
                break;

            case 'p':
                sol::stack::push(lua, va_arg(vargs, void*));
                break;

            case 's':
                sol::stack::push(lua, va_arg(vargs, const char*));
                break;

            case 'b':
                sol::stack::push(lua, (bool) va_arg(vargs, int));
                break;

            default:
                throw std::runtime_error("C++ call: Unknown argument identifier " + argl[index]);
        }
    }

    va_end(vargs);

    if (lua_pcall(lua, n_args, 1, 0) != 0)
        throw std::runtime_error(std::string("Lua error: ") + lua_tostring(lua, -1));
    boost::any ret;
    if (lua_isstring(lua, -1))       ret = boost::any(std::string(lua_tostring(lua, -1)));
    else if (lua_isnumber(lua, -1))  ret = boost::any(lua_tonumber(lua, -1));
    else if (lua_isboolean(lua, -1)) ret = boost::any((bool)lua_toboolean(lua, -1));
    lua_pop(lua, 1);
    return ret;
}

boost::any LangLua::Call(const char *name, const char *argl, const std::vector<boost::any> &args)
{
    int n_args = (int)(strlen(argl));

    lua_getglobal(lua, name);

    for (int index = 0; index < n_args; index++)
    {
        switch (argl[index])
        {
            case 'i':
                sol::stack::push(lua, boost::any_cast<unsigned int>(args.at(index)));
                break;

            case 'q':
                sol::stack::push(lua, boost::any_cast<signed int>(args.at(index)));
                break;

            case 'l':
                sol::stack::push(lua, boost::any_cast<unsigned long long>(args.at(index)));
                break;

            case 'w':
                sol::stack::push(lua, boost::any_cast<signed long long>(args.at(index)));
                break;

            case 'f':
                sol::stack::push(lua, boost::any_cast<double>(args.at(index)));
                break;

            case 'p':
                sol::stack::push(lua, boost::any_cast<void *>(args.at(index)));
                break;

            case 's':
                sol::stack::push(lua, boost::any_cast<const char *>(args.at(index)));
                break;

            case 'b':
                sol::stack::push(lua, (bool)boost::any_cast<int>(args.at(index)));
                break;
            default:
                throw std::runtime_error("Lua call: Unknown argument identifier " + argl[index]);
        }
    }

    if (lua_pcall(lua, n_args, 1, 0) != 0)
        throw std::runtime_error(std::string("Lua error: ") + lua_tostring(lua, -1));
    boost::any ret;
    if (lua_isstring(lua, -1))       ret = boost::any(std::string(lua_tostring(lua, -1)));
    else if (lua_isnumber(lua, -1))  ret = boost::any(lua_tonumber(lua, -1));
    else if (lua_isboolean(lua, -1)) ret = boost::any((bool)lua_toboolean(lua, -1));
    lua_pop(lua, 1);
    return ret;
}

void LangLua::AddPackagePath(const std::string& path)
{
    packagePath.emplace(path);
}

void LangLua::AddPackageCPath(const std::string& path)
{
    packageCPath.emplace(path);
}
