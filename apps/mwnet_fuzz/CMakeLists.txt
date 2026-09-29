if (NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "MWNet fuzzers require Clang and libFuzzer")
endif()

function(mwnet_add_fuzzer target source)
    add_executable(${target} ${source})
    target_compile_options(${target} PRIVATE
        -fsanitize=fuzzer,address,undefined
        -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE
        -fsanitize=fuzzer,address,undefined
        -fno-omit-frame-pointer)
endfunction()

mwnet_add_fuzzer(mwnet-packet-fuzz packet_fuzzer.cpp)
target_link_libraries(mwnet-packet-fuzz PRIVATE mwnet-protocol-codec)

mwnet_add_fuzzer(mwnet-transport-fuzz transport_fuzzer.cpp)
target_link_libraries(mwnet-transport-fuzz PRIVATE mwnet-transport)

if (TARGET mwnet-security)
    mwnet_add_fuzzer(mwnet-authentication-fuzz authentication_fuzzer.cpp)
    target_link_libraries(mwnet-authentication-fuzz PRIVATE mwnet-security)

    mwnet_add_fuzzer(mwnet-handshake-fuzz handshake_fuzzer.cpp)
    target_link_libraries(mwnet-handshake-fuzz PRIVATE mwnet-security)
endif()
