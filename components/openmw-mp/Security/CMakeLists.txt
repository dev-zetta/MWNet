add_library(tes3mp-security STATIC
    SecureSession.cpp
    ServerIdentity.cpp
    SodiumInit.cpp
    TrustStore.cpp
)

target_include_directories(tes3mp-security PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_features(tes3mp-security PUBLIC cxx_std_20)
target_link_libraries(tes3mp-security PUBLIC tes3mp-protocol-codec sodium::sodium)

if (MSVC)
    target_compile_options(tes3mp-security PRIVATE /W4 /WX)
else()
    target_compile_options(tes3mp-security PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
