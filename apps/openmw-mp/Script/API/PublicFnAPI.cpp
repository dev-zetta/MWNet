#include <Script/ScriptFunction.hpp>
#include "PublicFnAPI.hpp"

#include <utility>

std::unordered_map<std::string, std::unique_ptr<Public>> Public::publics;

Public::Public(ScriptFunc _public, const std::string &name, char ret_type, const std::string &def) : ScriptFunction(_public, ret_type, def)
{
}

Public::Public(ScriptFuncLua _public, lua_State *lua, const std::string &name, char ret_type, const std::string &def) : ScriptFunction(
        _public, lua, ret_type, def)
{
}

void Public::MakePublic(ScriptFunc callback, const std::string& name,
    char returnType, const std::string& definition)
{
    publics.insert_or_assign(name,
        std::unique_ptr<Public>(new Public(callback, name, returnType, definition)));
}

#if defined(ENABLE_LUA)
void Public::MakePublic(ScriptFuncLua callback, lua_State* lua,
    const std::string& name, char returnType, const std::string& definition)
{
    publics.insert_or_assign(name,
        std::unique_ptr<Public>(new Public(std::move(callback), lua, name, returnType, definition)));
}
#endif

boost::any Public::Call(const std::string &name, const std::vector<boost::any> &args)
{
    auto it = publics.find(name);
    if (it == publics.end())
        throw std::runtime_error("Public with name \"" + name + "\" does not exist");

    return it->second->ScriptFunction::Call(args);
}


const std::string &Public::GetDefinition(const std::string &name)
{
    auto it = publics.find(name);

    if (it == publics.end())
        throw std::runtime_error("Public with name \"" + name + "\" does not exist");

    return it->second->def;
}


bool Public::IsLua(const std::string &name)
{
#if !defined(ENABLE_LUA)
    return false;
#else
    auto it = publics.find(name);
    if (it == publics.end())
        throw std::runtime_error("Public with name \"" + name + "\" does not exist");

    return it->second->script_type == SCRIPT_LUA;
#endif
}

void Public::DeleteAll()
{
    publics.clear();
}
