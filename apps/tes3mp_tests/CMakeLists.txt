add_executable(tes3mp-tests
    active_effects.cpp
    authority.cpp
    cast.cpp
    combat.cpp
    inventory.cpp
    justice.cpp
    main.cpp
    lifecycle.cpp
    movement.cpp
    object_state.cpp
    ownership.cpp
    persistence.cpp
    protocol.cpp
    session.cpp
    transport.cpp
)

target_include_directories(tes3mp-tests PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(tes3mp-tests PRIVATE tes3mp-mechanics tes3mp-persistence
    tes3mp-protocol-codec tes3mp-session tes3mp-transport)

if (TARGET tes3mp-gns-transport)
    target_sources(tes3mp-tests PRIVATE authentication.cpp gns_transport.cpp security.cpp)
    target_link_libraries(tes3mp-tests PRIVATE tes3mp-gns-transport
        tes3mp-protocol11-endpoint tes3mp-security)
    target_compile_definitions(tes3mp-tests PRIVATE TES3MP_HAS_GNS_TRANSPORT=1)
endif()

if (MSVC)
    target_compile_options(tes3mp-tests PRIVATE /W4 /WX)
else()
    target_compile_options(tes3mp-tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_test(NAME tes3mp-unit COMMAND tes3mp-tests)
