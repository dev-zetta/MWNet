#ifndef PLUGINSYSTEM3_PUBLICFNAPI_HPP
#define PLUGINSYSTEM3_PUBLICFNAPI_HPP

#include <memory>
#include <unordered_map>
#include <Script/ScriptFunction.hpp>


class Public : public ScriptFunction
{
private:
    static std::unordered_map<std::string, std::unique_ptr<Public>> publics;

    Public(ScriptFunc _public, const std::string &name, char ret_type, const std::string &def);
#if defined(ENABLE_LUA)
    Public(ScriptFuncLua _public, lua_State *lua, const std::string &name, char ret_type, const std::string &def);
#endif

public:
    ~Public() override = default;

    static void MakePublic(ScriptFunc callback, const std::string& name,
        char returnType, const std::string& definition);
#if defined(ENABLE_LUA)
    static void MakePublic(ScriptFuncLua callback, lua_State* lua,
        const std::string& name, char returnType, const std::string& definition);
#endif

    static boost::any Call(const std::string &name, const std::vector<boost::any> &args);

    static const std::string& GetDefinition(const std::string& name);

    static bool IsLua(const std::string &name);

    static void DeleteAll();
};

#endif //PLUGINSYSTEM3_PUBLICFNAPI_HPP
