#include <components/openmw-mp/Mechanics/ActorMagicRegistry.hpp>

#include <iostream>
#include <string>

namespace
{
    using namespace mwmp::mechanics;

    int sFailures = 0;

    void expect(bool condition, const char* expression, int line)
    {
        if (condition)
            return;
        std::cerr << "actor_magic.cpp:" << line << ": expectation failed: "
                  << expression << '\n';
        ++sFailures;
    }

#define EXPECT(condition) expect((condition), #condition, __LINE__)

    ActorMagicTemplate mage()
    {
        ActorMagicTemplate actor;
        actor.refId = "mage";
        actor.maximumHealth = 50;
        actor.maximumMagicka = 100;
        actor.maximumFatigue = 150;
        actor.willpower = 60;
        actor.luck = 40;
        actor.enchantSkill = 35;
        actor.magicSkills.emplace("destruction", 55);
        actor.spells.emplace("fireball");
        actor.inventory.push_back({ "ring", {}, -1, -1, 1 });
        return actor;
    }

    void testBoundedCanonicalTemplates()
    {
        ActorMagicRegistry registry(1);
        EXPECT(registry.upsert(mage()));
        EXPECT(registry.size() == 1);
        EXPECT(registry.knowsSpell("mage", "fireball"));
        EXPECT(!registry.knowsSpell("mage", "god_spell"));
        EXPECT(registry.find("mage")->maximumMagicka == 100);

        ActorMagicTemplate replacement = mage();
        replacement.maximumMagicka = 125;
        EXPECT(registry.upsert(replacement));
        EXPECT(registry.find("mage")->maximumMagicka == 125);

        ActorMagicTemplate second = mage();
        second.refId = "second";
        EXPECT(!registry.upsert(second));
        EXPECT(registry.erase("mage"));
        EXPECT(registry.upsert(second));
    }

    void testInvalidTemplatesFailClosed()
    {
        ActorMagicRegistry registry;
        ActorMagicTemplate actor = mage();
        actor.maximumHealth = -1;
        EXPECT(!registry.upsert(actor));
        actor = mage();
        actor.spells.emplace(std::string(4097, 'x'));
        EXPECT(!registry.upsert(actor));
        actor = mage();
        actor.inventory.front().count = 0;
        EXPECT(!registry.upsert(actor));
        EXPECT(registry.size() == 0);
    }
}

int runActorMagicTests()
{
    testBoundedCanonicalTemplates();
    testInvalidTemplatesFailClosed();
    return sFailures;
}
