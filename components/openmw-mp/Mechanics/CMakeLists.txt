add_library(mwnet-mechanics STATIC
    ActiveEffectLedger.cpp
    ActorMagicRegistry.cpp
    ActorStateLedger.cpp
    CastIntentValidator.cpp
    CombatResolver.cpp
    EquipmentLedger.cpp
    InventoryAcknowledgementLedger.cpp
    InventoryLedger.cpp
    ItemUseValidator.cpp
    JusticeLedger.cpp
    MovementValidator.cpp
    ObjectStateLedger.cpp
    PlayerProgressionLedger.cpp
    PlayerLifecycle.cpp
    ShapeshiftLedger.cpp
    SpellbookLedger.cpp
    SpellResolver.cpp
)

target_include_directories(mwnet-mechanics PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_features(mwnet-mechanics PUBLIC cxx_std_20)

if (MSVC)
    target_compile_options(mwnet-mechanics PRIVATE /W4 /WX)
else()
    target_compile_options(mwnet-mechanics PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
