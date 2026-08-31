#include <components/openmw-mp/Mechanics/EquipmentLedger.hpp>

#include <iostream>
#include <limits>
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
        std::cerr << "equipment.cpp:" << line << ": expectation failed: "
                  << expression << '\n';
        ++sFailures;
    }

#define EXPECT(condition) expect((condition), #condition, __LINE__)

    InventoryItem inventoryItem(std::string refId, std::int64_t count,
        std::int32_t charge = -1, double enchantmentCharge = -1)
    {
        InventoryItem result;
        result.refId = std::move(refId);
        result.count = count;
        result.charge = charge;
        result.enchantmentCharge = enchantmentCharge;
        return result;
    }

    EquipmentItem equipmentItem(std::string refId, std::int64_t count,
        std::int32_t charge = -1, double enchantmentCharge = -1)
    {
        return { std::move(refId), count, charge, enchantmentCharge };
    }

    std::vector<EquipmentChange> fullEquipment()
    {
        std::vector<EquipmentChange> result;
        for (std::size_t slot = 0; slot < EquipmentLedger::SlotCount; ++slot)
            result.push_back({ slot, {} });
        return result;
    }

    void testFullAndPartialChanges()
    {
        EquipmentLedger ledger;
        auto full = fullEquipment();
        full[0].item = equipmentItem("iron_cuirass", 1, 100);
        full[8].item = equipmentItem("common_ring", 1);
        const std::vector<InventoryItem> inventory{
            inventoryItem("iron_cuirass", 1, 100),
            inventoryItem("common_ring", 2),
        };

        EXPECT(ledger.preview(7, true, full, inventory).applied());
        EXPECT(!ledger.snapshot(7).has_value());
        EXPECT(ledger.apply(7, true, full, inventory).applied());
        EXPECT(ledger.snapshot(7)->at(0).refId == "iron_cuirass");

        EXPECT(ledger.apply(7, false,
                   { { 0, {} }, { 9, equipmentItem("common_ring", 1) } },
                   inventory).applied());
        const auto snapshot = ledger.snapshot(7);
        EXPECT(snapshot->at(0).empty());
        EXPECT(snapshot->at(8).refId == "common_ring");
        EXPECT(snapshot->at(9).refId == "common_ring");
    }

    void testInventoryOwnership()
    {
        EquipmentLedger ledger;
        const std::vector<InventoryItem> oneRing{ inventoryItem("common_ring", 1) };
        EXPECT(ledger.apply(1, false,
                   { { 8, equipmentItem("common_ring", 1) } }, oneRing).applied());
        EXPECT(ledger.preview(1, false,
                   { { 9, equipmentItem("common_ring", 1) } }, oneRing).decision
            == EquipmentDecision::ItemNotInInventory);
        EXPECT(ledger.snapshot(1)->at(9).empty());
        EXPECT(ledger.validateInventory(1, oneRing).applied());
        EXPECT(ledger.validateInventory(1, {}).decision
            == EquipmentDecision::ItemNotInInventory);
        EXPECT(ledger.validateInventory(99, {}).applied());

        const std::vector<InventoryItem> splitSouls{
            { "amulet", "rat", -1, -1, 1 },
            { "amulet", "", -1, -1, 1 },
        };
        EXPECT(ledger.apply(2, false,
                   { { 8, equipmentItem("amulet", 1) },
                     { 9, equipmentItem("amulet", 1) } }, splitSouls).applied());
    }

    void testRejectsMalformedChanges()
    {
        EquipmentLedger ledger;
        const std::vector<InventoryItem> inventory{ inventoryItem("robe", 1) };
        EXPECT(ledger.preview(0, false,
                   { { 0, equipmentItem("robe", 1) } }, inventory).decision
            == EquipmentDecision::InvalidOwner);
        EXPECT(ledger.preview(1, false, {}, inventory).decision
            == EquipmentDecision::InvalidChangeSet);
        EXPECT(ledger.preview(1, false,
                   { { EquipmentLedger::SlotCount, {} } }, inventory).decision
            == EquipmentDecision::InvalidSlot);
        EXPECT(ledger.preview(1, false,
                   { { 0, {} }, { 0, {} } }, inventory).decision
            == EquipmentDecision::DuplicateSlot);

        EquipmentItem invalid = equipmentItem("robe", 1);
        invalid.enchantmentCharge = std::numeric_limits<double>::quiet_NaN();
        EXPECT(ledger.preview(1, false, { { 0, invalid } }, inventory).decision
            == EquipmentDecision::InvalidItem);

        auto incompleteFull = fullEquipment();
        incompleteFull.pop_back();
        EXPECT(ledger.preview(1, true, incompleteFull, inventory).decision
            == EquipmentDecision::InvalidChangeSet);
    }

    void testCleanup()
    {
        EquipmentLedger ledger;
        auto full = fullEquipment();
        EXPECT(ledger.apply(5, true, full, {}).applied());
        EXPECT(ledger.size() == 1);
        EXPECT(ledger.erase(5));
        EXPECT(!ledger.erase(5));
        EXPECT(ledger.size() == 0);
        EXPECT(std::string(describe(EquipmentDecision::ItemNotInInventory))
            == "an equipped item is not present in the canonical inventory");
    }

    void testSwap()
    {
        EquipmentLedger first;
        EquipmentLedger second;
        const std::vector<InventoryItem> inventory{
            inventoryItem("iron_sword", 1) };
        EXPECT(first.apply(1, false,
                   { { 0, equipmentItem("iron_sword", 1) } }, inventory).applied());
        second.swap(first);
        EXPECT(!first.snapshot(1).has_value());
        EXPECT(second.snapshot(1)->at(0).refId == "iron_sword");
    }
}

int runEquipmentTests()
{
    testFullAndPartialChanges();
    testInventoryOwnership();
    testRejectsMalformedChanges();
    testCleanup();
    testSwap();
    return sFailures;
}
