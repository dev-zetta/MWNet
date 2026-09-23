add_executable(tes3mp-tests
    active_effects.cpp
    actor_magic.cpp
    actor_state.cpp
    authority.cpp
    cast.cpp
    combat.cpp
    equipment.cpp
    inventory.cpp
    item_use.cpp
    justice.cpp
    main.cpp
    lifecycle.cpp
    lua_policy.cpp
    movement.cpp
    metrics.cpp
    object_state.cpp
    ownership.cpp
    persistence.cpp
    protocol.cpp
    progression.cpp
    session.cpp
    shapeshift.cpp
    soak_memory.cpp
    spell.cpp
    spellbook.cpp
    transport.cpp
)

target_include_directories(tes3mp-tests PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(tes3mp-tests PRIVATE tes3mp-mechanics tes3mp-metrics tes3mp-persistence
    tes3mp-protocol-codec tes3mp-session tes3mp-transport)

if (NOT TES3MP_TESTS_ONLY)
    target_sources(tes3mp-tests PRIVATE
        timed_log.cpp
        ../../components/openmw-mp/TimedLog.cpp)
    target_compile_definitions(tes3mp-tests PRIVATE TES3MP_HAS_TIMED_LOG_TESTS=1)
endif()

if (TARGET tes3mp-security)
    target_sources(tes3mp-tests PRIVATE authentication.cpp security.cpp)
    target_link_libraries(tes3mp-tests PRIVATE tes3mp-security)
    target_compile_definitions(tes3mp-tests PRIVATE TES3MP_HAS_SECURITY_TESTS=1)
endif()

if (TARGET tes3mp-gns-transport)
    target_sources(tes3mp-tests PRIVATE gns_transport.cpp)
    target_link_libraries(tes3mp-tests PRIVATE tes3mp-gns-transport
        tes3mp-game-endpoint tes3mp-security)
    target_compile_definitions(tes3mp-tests PRIVATE TES3MP_HAS_GNS_TRANSPORT=1)

    add_executable(tes3mp-headless-integration headless_integration.cpp)
    target_include_directories(tes3mp-headless-integration PRIVATE ${CMAKE_SOURCE_DIR})
    target_link_libraries(tes3mp-headless-integration PRIVATE tes3mp-mechanics
        tes3mp-metrics tes3mp-game-endpoint tes3mp-security)

    if (MSVC)
        target_compile_options(tes3mp-headless-integration PRIVATE /W4 /WX)
    else()
        target_compile_options(tes3mp-headless-integration PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
    endif()

    add_test(NAME tes3mp-headless COMMAND tes3mp-headless-integration
        --cycles 2 --clients 2 --packet-loss-percent 25)
    set_tests_properties(tes3mp-headless PROPERTIES TIMEOUT 90)
endif()

if (MSVC)
    target_compile_options(tes3mp-tests PRIVATE /W4 /WX)
else()
    target_compile_options(tes3mp-tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_test(NAME tes3mp-unit COMMAND tes3mp-tests)

if (TARGET components)
    add_executable(tes3mp-actor-packet-tests actor_packets.cpp)
    target_include_directories(tes3mp-actor-packet-tests PRIVATE ${CMAKE_SOURCE_DIR})
    target_link_libraries(tes3mp-actor-packet-tests PRIVATE components)
    add_test(NAME tes3mp-actor-packets COMMAND tes3mp-actor-packet-tests)

    add_executable(tes3mp-magic-content-tests magic_content.cpp
        ../openmw-mp/MagicContent.cpp)
    target_include_directories(tes3mp-magic-content-tests PRIVATE ${CMAKE_SOURCE_DIR})
    target_link_libraries(tes3mp-magic-content-tests PRIVATE components tes3mp-mechanics)
    add_test(NAME tes3mp-magic-content COMMAND tes3mp-magic-content-tests)
endif()

add_executable(tes3mp-persistence-fault persistence_fault.cpp)
target_include_directories(tes3mp-persistence-fault PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(tes3mp-persistence-fault PRIVATE tes3mp-persistence)

if (MSVC)
    target_compile_options(tes3mp-persistence-fault PRIVATE /W4 /WX)
else()
    target_compile_options(tes3mp-persistence-fault PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()

add_test(NAME tes3mp-persistence-fault COMMAND tes3mp-persistence-fault)
set_tests_properties(tes3mp-persistence-fault PROPERTIES TIMEOUT 30)

if (TARGET tes3mp-directory-core)
    add_executable(tes3mp-discovery-tests discovery.cpp)
    target_link_libraries(tes3mp-discovery-tests PRIVATE tes3mp-directory-core)
    add_test(NAME tes3mp-discovery COMMAND tes3mp-discovery-tests)
    set_tests_properties(tes3mp-discovery PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
endif()
