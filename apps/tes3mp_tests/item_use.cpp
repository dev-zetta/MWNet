#include <components/openmw-mp/Mechanics/ItemUseValidator.hpp>

#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace
{
    using namespace mwmp::mechanics;

    int sFailures = 0;

    void expect(bool condition, const char* expression, int line)
    {
        if (condition)
            return;
        std::cerr << "item_use.cpp:" << line << ": expectation failed: "
                  << expression << '\n';
        ++sFailures;
    }

#define EXPECT(condition) expect((condition), #condition, __LINE__)

    ItemUseIntent intent()
    {
        return { { "p_restore_health_s", {}, -1, -1, 3 }, false, 0 };
    }

    void testExactCanonicalStackIsRequired()
    {
        ItemUseValidator validator;
        const ItemUseIntent valid = intent();
        const std::optional<std::vector<InventoryItem>> inventory{
            std::vector<InventoryItem>{ valid.item }
        };
        EXPECT(validator.validate(valid, inventory) == ItemUseDecision::Accepted);
        EXPECT(validator.validate(valid, std::nullopt)
            == ItemUseDecision::MissingInventory);

        ItemUseIntent forged = valid;
        forged.item.count = 2;
        EXPECT(validator.validate(forged, inventory) == ItemUseDecision::MissingItem);
        forged = valid;
        forged.item.refId = "gold_001";
        EXPECT(validator.validate(forged, inventory) == ItemUseDecision::MissingItem);
    }

    void testMalformedIntentFailsClosed()
    {
        ItemUseValidator validator;
        ItemUseIntent malformed = intent();
        const std::optional<std::vector<InventoryItem>> inventory{
            std::vector<InventoryItem>{ malformed.item }
        };
        malformed.drawState = ItemUseValidator::MaximumDrawState + 1;
        EXPECT(validator.validate(malformed, inventory)
            == ItemUseDecision::InvalidDrawState);
        malformed = intent();
        malformed.item.count = 0;
        EXPECT(validator.validate(malformed, inventory) == ItemUseDecision::InvalidItem);
        malformed = intent();
        malformed.item.enchantmentCharge
            = std::numeric_limits<double>::quiet_NaN();
        EXPECT(validator.validate(malformed, inventory) == ItemUseDecision::InvalidItem);
        EXPECT(std::string(describe(ItemUseDecision::MissingItem))
            == "the exact item stack is not in the canonical inventory");
    }
}

int runItemUseTests()
{
    testExactCanonicalStackIsRequired();
    testMalformedIntentFailsClosed();
    return sFailures;
}
