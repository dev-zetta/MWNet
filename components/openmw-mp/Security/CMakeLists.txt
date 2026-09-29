add_library(mwnet-security STATIC
    AccountAuthentication.cpp
    AccountStore.cpp
    AuthenticationMessages.cpp
    AuthenticationRateLimiter.cpp
    PasswordHash.cpp
    SecureSession.cpp
    ServerAuthenticationService.cpp
    ServerIdentity.cpp
    SodiumInit.cpp
    TrustStore.cpp
)

target_include_directories(mwnet-security PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_features(mwnet-security PUBLIC cxx_std_20)
target_link_libraries(mwnet-security PUBLIC mwnet-persistence mwnet-protocol-codec sodium::sodium)

if (MSVC)
    target_compile_options(mwnet-security PRIVATE /W4 /WX)
else()
    target_compile_options(mwnet-security PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
