add_library(tes3mp-session STATIC
    SessionState.cpp
    SessionTransport.cpp
)

target_include_directories(tes3mp-session PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_features(tes3mp-session PUBLIC cxx_std_20)
target_link_libraries(tes3mp-session PUBLIC tes3mp-protocol-codec tes3mp-transport)

if (MSVC)
    target_compile_options(tes3mp-session PRIVATE /W4 /WX)
else()
    target_compile_options(tes3mp-session PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
