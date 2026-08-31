add_library(tes3mp-mechanics STATIC
    ActiveEffectLedger.cpp
    CastIntentValidator.cpp
    CombatResolver.cpp
    EquipmentLedger.cpp
    InventoryLedger.cpp
    JusticeLedger.cpp
    MovementValidator.cpp
    ObjectStateLedger.cpp
    PlayerLifecycle.cpp
)

target_include_directories(tes3mp-mechanics PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_features(tes3mp-mechanics PUBLIC cxx_std_20)

if (MSVC)
    target_compile_options(tes3mp-mechanics PRIVATE /W4 /WX)
else()
    target_compile_options(tes3mp-mechanics PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
