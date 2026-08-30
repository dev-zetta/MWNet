add_executable(tes3mp-tests
    main.cpp
    protocol.cpp
)

target_include_directories(tes3mp-tests PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(tes3mp-tests PRIVATE tes3mp-protocol-codec)

if (MSVC)
    target_compile_options(tes3mp-tests PRIVATE /W4 /WX)
else()
    target_compile_options(tes3mp-tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_test(NAME tes3mp-version COMMAND tes3mp-tests)
