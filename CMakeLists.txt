cmake_minimum_required(VERSION 3.20)
project(phimantec_c11_appliance VERSION 0.3.17 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(PHIMANTEC_ASAN "Build with AddressSanitizer/UBSan" OFF)

find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

add_compile_definitions(_POSIX_C_SOURCE=200809L)

add_compile_options(
  -Wall
  -Wextra
  -Wpedantic
  -Werror
  -Wshadow
  -Wconversion
  -Wstrict-prototypes
  -Wmissing-prototypes
  -Wno-unused-parameter
)

if(PHIMANTEC_ASAN)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g)
  add_link_options(-fsanitize=address,undefined)
endif()

add_library(phimantec_core STATIC
  shared/src/phim_base64.c
  shared/src/phim_config.c
  shared/src/phim_eve.c
  shared/src/phim_flowbus.c
  shared/src/phim_h1.c
  shared/src/phim_log.c
  shared/src/phim_mem.c
  shared/src/phim_socket.c
  shared/src/phim_string.c
  shared/src/phim_time.c
  appliance/src/phim_blockpage.c
  appliance/src/phim_cert.c
  appliance/src/phim_connect_parser.c
  appliance/src/phim_lane.c
  appliance/src/phim_proxy_out.c
  appliance/src/phim_tls.c
  appliance/src/phim_verdict.c
)

target_include_directories(phimantec_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/shared/include
  ${CMAKE_CURRENT_SOURCE_DIR}/appliance/include
  ${CMAKE_CURRENT_SOURCE_DIR}/suricata-node/include
)

target_link_libraries(phimantec_core PUBLIC OpenSSL::SSL OpenSSL::Crypto Threads::Threads)

add_executable(phimantec-proxyd
  appliance/src/main.c
  appliance/src/phim_proxy_worker.c
)
target_link_libraries(phimantec-proxyd PRIVATE phimantec_core)

add_executable(phimantec-lane-sink
  suricata-node/src/lane_sink_main.c
)
target_link_libraries(phimantec-lane-sink PRIVATE phimantec_core)

add_executable(phimantec-inspectord
  suricata-node/src/inspectord_main.c
)
target_link_libraries(phimantec-inspectord PRIVATE phimantec_core)

add_executable(phimantec-flowbus-probe
  tools/flowbus_probe_main.c
)
target_link_libraries(phimantec-flowbus-probe PRIVATE phimantec_core)

include(CTest)
if(BUILD_TESTING)
  add_executable(test_connect_parser tests/test_connect_parser.c)
  target_link_libraries(test_connect_parser PRIVATE phimantec_core)
  add_test(NAME connect_parser COMMAND test_connect_parser)

  add_executable(test_h1_parser tests/test_h1_parser.c)
  target_link_libraries(test_h1_parser PRIVATE phimantec_core)
  add_test(NAME h1_parser COMMAND test_h1_parser)

  add_executable(test_flowbus_hmac tests/test_flowbus_hmac.c)
  target_link_libraries(test_flowbus_hmac PRIVATE phimantec_core)
  add_test(NAME flowbus_hmac COMMAND test_flowbus_hmac)

  add_executable(test_flowbus_write_read tests/test_flowbus_write_read.c)
  target_link_libraries(test_flowbus_write_read PRIVATE phimantec_core)
  add_test(NAME flowbus_write_read COMMAND test_flowbus_write_read)

  add_executable(test_config_timeouts tests/test_config_timeouts.c)
  target_link_libraries(test_config_timeouts PRIVATE phimantec_core)
  add_test(NAME config_timeouts COMMAND test_config_timeouts)

  add_executable(test_eve_correlation tests/test_eve_correlation.c)
  target_link_libraries(test_eve_correlation PRIVATE phimantec_core)
  add_test(NAME eve_correlation COMMAND test_eve_correlation)

  add_executable(test_verdict_timeout tests/test_verdict_timeout.c)
  target_link_libraries(test_verdict_timeout PRIVATE phimantec_core)
  add_test(NAME verdict_timeout COMMAND test_verdict_timeout)

  add_executable(test_suricata_only_gate tests/test_suricata_only_gate.c)
  target_link_libraries(test_suricata_only_gate PRIVATE phimantec_core)
  add_test(NAME suricata_only_gate COMMAND test_suricata_only_gate)

  add_executable(test_h1_chunked_decoder tests/test_h1_chunked_decoder.c)
  target_link_libraries(test_h1_chunked_decoder PRIVATE phimantec_core)
  add_test(NAME h1_chunked_decoder COMMAND test_h1_chunked_decoder)

  add_executable(test_cert_generation tests/test_cert_generation.c)
  target_link_libraries(test_cert_generation PRIVATE phimantec_core)
  add_test(NAME cert_generation COMMAND test_cert_generation)
endif()

install(TARGETS phimantec-proxyd phimantec-lane-sink phimantec-inspectord phimantec-flowbus-probe RUNTIME DESTINATION bin)
install(DIRECTORY config/ DESTINATION etc/phimantec)
install(DIRECTORY docs/ DESTINATION share/doc/phimantec)
install(DIRECTORY suricata-monitor/ DESTINATION share/phimantec/suricata-monitor)
