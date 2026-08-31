if (NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "TES3MP fuzzers require Clang and libFuzzer")
endif()

function(tes3mp_add_fuzzer target source)
    add_executable(${target} ${source})
    target_compile_options(${target} PRIVATE
        -fsanitize=fuzzer,address,undefined
        -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE
        -fsanitize=fuzzer,address,undefined
        -fno-omit-frame-pointer)
endfunction()

tes3mp_add_fuzzer(tes3mp-packet-fuzz packet_fuzzer.cpp)
target_link_libraries(tes3mp-packet-fuzz PRIVATE tes3mp-protocol-codec)

tes3mp_add_fuzzer(tes3mp-transport-fuzz transport_fuzzer.cpp)
target_link_libraries(tes3mp-transport-fuzz PRIVATE tes3mp-transport)

if (TARGET tes3mp-security)
    tes3mp_add_fuzzer(tes3mp-authentication-fuzz authentication_fuzzer.cpp)
    target_link_libraries(tes3mp-authentication-fuzz PRIVATE tes3mp-security)

    tes3mp_add_fuzzer(tes3mp-handshake-fuzz handshake_fuzzer.cpp)
    target_link_libraries(tes3mp-handshake-fuzz PRIVATE tes3mp-security)
endif()
