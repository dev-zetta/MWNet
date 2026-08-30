add_library(tes3mp-protocol-codec STATIC
    EndpointSecurity.cpp
    MessageType.cpp
    PacketCodec.cpp
    RateLimits.cpp
)

target_include_directories(tes3mp-protocol-codec PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_features(tes3mp-protocol-codec PUBLIC cxx_std_20)

if (MSVC)
    target_compile_options(tes3mp-protocol-codec PRIVATE /W4 /WX)
else()
    target_compile_options(tes3mp-protocol-codec PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
