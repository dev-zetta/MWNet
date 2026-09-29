add_executable(mwnet-tests
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
    native_function.cpp
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

target_include_directories(mwnet-tests PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(mwnet-tests PRIVATE mwnet-mechanics mwnet-metrics mwnet-persistence
    mwnet-protocol-codec mwnet-session mwnet-transport)

if (NOT MWNET_TESTS_ONLY)
    target_sources(mwnet-tests PRIVATE
        timed_log.cpp
        ../../components/openmw-mp/TimedLog.cpp)
    target_compile_definitions(mwnet-tests PRIVATE MWNET_HAS_TIMED_LOG_TESTS=1)
endif()

if (TARGET mwnet-security)
    target_sources(mwnet-tests PRIVATE authentication.cpp security.cpp)
    target_link_libraries(mwnet-tests PRIVATE mwnet-security)
    target_compile_definitions(mwnet-tests PRIVATE MWNET_HAS_SECURITY_TESTS=1)
endif()

if (TARGET mwnet-gns-transport)
    target_sources(mwnet-tests PRIVATE gns_transport.cpp)
    target_link_libraries(mwnet-tests PRIVATE mwnet-gns-transport
        mwnet-game-endpoint mwnet-security)
    target_compile_definitions(mwnet-tests PRIVATE MWNET_HAS_GNS_TRANSPORT=1)

    add_executable(mwnet-headless-integration headless_integration.cpp)
    target_include_directories(mwnet-headless-integration PRIVATE ${CMAKE_SOURCE_DIR})
    target_link_libraries(mwnet-headless-integration PRIVATE mwnet-mechanics
        mwnet-metrics mwnet-game-endpoint mwnet-security)

    if (MSVC)
        target_compile_options(mwnet-headless-integration PRIVATE /W4 /WX)
    else()
        target_compile_options(mwnet-headless-integration PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
    endif()

    add_test(NAME mwnet-headless COMMAND mwnet-headless-integration
        --cycles 2 --clients 2 --packet-loss-percent 25)
    set_tests_properties(mwnet-headless PROPERTIES TIMEOUT 90)
endif()

if (MSVC)
    target_compile_options(mwnet-tests PRIVATE /W4 /WX)
else()
    target_compile_options(mwnet-tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_test(NAME mwnet-unit COMMAND mwnet-tests)

if (TARGET components)
    add_executable(mwnet-actor-packet-tests actor_packets.cpp)
    target_include_directories(mwnet-actor-packet-tests PRIVATE ${CMAKE_SOURCE_DIR})
    target_link_libraries(mwnet-actor-packet-tests PRIVATE components)
    add_test(NAME mwnet-actor-packets COMMAND mwnet-actor-packet-tests)

    add_executable(mwnet-magic-content-tests magic_content.cpp
        ../openmw-mp/MagicContent.cpp)
    target_include_directories(mwnet-magic-content-tests PRIVATE ${CMAKE_SOURCE_DIR})
    target_link_libraries(mwnet-magic-content-tests PRIVATE components mwnet-mechanics)
    add_test(NAME mwnet-magic-content COMMAND mwnet-magic-content-tests)
endif()

add_executable(mwnet-persistence-fault persistence_fault.cpp)
target_include_directories(mwnet-persistence-fault PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(mwnet-persistence-fault PRIVATE mwnet-persistence)

if (MSVC)
    target_compile_options(mwnet-persistence-fault PRIVATE /W4 /WX)
else()
    target_compile_options(mwnet-persistence-fault PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()

add_test(NAME mwnet-persistence-fault COMMAND mwnet-persistence-fault)
set_tests_properties(mwnet-persistence-fault PROPERTIES TIMEOUT 30)

if (TARGET mwnet-directory-core)
    add_executable(mwnet-discovery-tests discovery.cpp)
    target_link_libraries(mwnet-discovery-tests PRIVATE mwnet-directory-core)
    add_test(NAME mwnet-discovery COMMAND mwnet-discovery-tests)
    set_tests_properties(mwnet-discovery PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
endif()
