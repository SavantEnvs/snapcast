# Fuzz build for snapcast: compiles the project's own sources (common/, client/decoder/, server/) with
# the fuzz flags into static libraries and links each harness in mayhem/harnesses/ against them, twice:
# <name> (libFuzzer) and <name>-standalone (run-once reproducer). Driven by mayhem/build.sh, which
# passes SANITIZER_FLAGS/DEBUG_FLAGS/coverage through CMAKE_C(XX)_FLAGS and LIB_FUZZING_ENGINE /
# STANDALONE_MAIN_OBJ as cache variables. The upstream definitions mirror the root CMakeLists.txt.
cmake_minimum_required(VERSION 3.16)
project(snapcast_fuzz C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
get_filename_component(SNAP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

set(LIB_FUZZING_ENGINE "-fsanitize=fuzzer" CACHE STRING "libFuzzer link flag")
set(STANDALONE_MAIN_OBJ "" CACHE FILEPATH "compiled StandaloneFuzzTargetMain.c object")
set(STANDALONE_EXTRA "" CACHE STRING "extra link input for the standalone reproducers")
set(LSAN_OFF_SRC "${CMAKE_CURRENT_SOURCE_DIR}/lsan_off.cc" CACHE FILEPATH "TU defining __lsan_is_turned_off()")

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Boost 1.74 REQUIRED)
pkg_check_modules(SNAPDEPS REQUIRED flac ogg vorbis vorbisenc opus soxr expat)

add_compile_definitions(
  VERSION="0.35.0" HAS_DAEMON HAS_EXPAT HAS_FLAC HAS_OGG HAS_OPENSSL HAS_OPUS HAS_SOXR HAS_VORBIS
  HAS_VORBIS_ENC BOOST_ERROR_CODE_HEADER_ONLY BOOST_ASIO_NO_TS_EXECUTORS BOOST_PROCESS_VERSION=1)
include_directories(${SNAP_ROOT} ${SNAP_ROOT}/server ${SNAP_ROOT}/client ${CMAKE_CURRENT_SOURCE_DIR}/harnesses)
include_directories(SYSTEM ${Boost_INCLUDE_DIRS} ${SNAPDEPS_INCLUDE_DIRS})
# The project code itself carries SanCov edges, not only the harness TU.
add_compile_options(-fsanitize=fuzzer-no-link)

add_library(snap_common STATIC
  ${SNAP_ROOT}/common/resampler.cpp ${SNAP_ROOT}/common/sample_format.cpp ${SNAP_ROOT}/common/base64.cpp
  ${SNAP_ROOT}/common/stream_uri.cpp ${SNAP_ROOT}/common/utils/string_utils.cpp
  ${SNAP_ROOT}/common/utils/file_utils.cpp ${SNAP_ROOT}/common/daemon.cpp)

add_library(snap_decoder STATIC
  ${SNAP_ROOT}/client/decoder/flac_decoder.cpp ${SNAP_ROOT}/client/decoder/ogg_decoder.cpp
  ${SNAP_ROOT}/client/decoder/opus_decoder.cpp ${SNAP_ROOT}/client/decoder/pcm_decoder.cpp
  ${SNAP_ROOT}/client/decoder/null_decoder.cpp)

# server/CMakeLists.txt SERVER_SOURCES (with flac/ogg/opus encoders, no ALSA/Avahi/JACK/PipeWire),
# minus snapserver.cpp (main).
set(S ${SNAP_ROOT}/server)
add_library(snap_server STATIC
  ${S}/authinfo.cpp ${S}/config.cpp ${S}/control_server.cpp ${S}/control_requests.cpp
  ${S}/control_session_tcp.cpp ${S}/control_session_http.cpp ${S}/control_session_ws.cpp ${S}/server.cpp
  ${S}/stream_server.cpp ${S}/stream_session.cpp ${S}/stream_session_tcp.cpp ${S}/stream_session_ws.cpp
  ${S}/encoder/encoder_factory.cpp ${S}/encoder/pcm_encoder.cpp ${S}/encoder/null_encoder.cpp
  ${S}/encoder/flac_encoder.cpp ${S}/encoder/ogg_encoder.cpp ${S}/encoder/opus_encoder.cpp
  ${S}/streamreader/control_error.cpp ${S}/streamreader/stream_control.cpp
  ${S}/streamreader/stream_manager.cpp ${S}/streamreader/pcm_stream.cpp ${S}/streamreader/tcp_stream.cpp
  ${S}/streamreader/pipe_stream.cpp ${S}/streamreader/file_stream.cpp ${S}/streamreader/airplay_stream.cpp
  ${S}/streamreader/librespot_stream.cpp ${S}/streamreader/meta_stream.cpp ${S}/streamreader/watchdog.cpp
  ${S}/streamreader/properties.cpp ${S}/streamreader/metadata.cpp ${S}/streamreader/process_stream.cpp)

set(SNAP_LIBS snap_server snap_decoder snap_common ${SNAPDEPS_LIBRARIES} OpenSSL::SSL OpenSSL::Crypto
    Threads::Threads)

add_library(lsan_off OBJECT ${LSAN_OFF_SRC})

function(snap_fuzzer name src)
  add_executable(${name} ${src} $<TARGET_OBJECTS:lsan_off>)
  target_compile_definitions(${name} PRIVATE ${ARGN})
  target_link_libraries(${name} PRIVATE ${SNAP_LIBS})
  target_link_options(${name} PRIVATE ${LIB_FUZZING_ENGINE})
  add_executable(${name}-standalone ${src} ${STANDALONE_MAIN_OBJ} $<TARGET_OBJECTS:lsan_off>)
  target_compile_definitions(${name}-standalone PRIVATE ${ARGN})
  target_link_libraries(${name}-standalone PRIVATE ${SNAP_LIBS} ${STANDALONE_EXTRA})
endfunction()

set(H ${CMAKE_CURRENT_SOURCE_DIR}/harnesses)
snap_fuzzer(fuzz_message ${H}/fuzz_message.cpp)
snap_fuzzer(fuzz_opus_decoder ${H}/fuzz_decoder.cpp SNAP_DECODER=OpusDecoder SNAP_CODEC="opus")
snap_fuzzer(fuzz_pcm_decoder ${H}/fuzz_decoder.cpp SNAP_DECODER=PcmDecoder SNAP_CODEC="pcm")
snap_fuzzer(fuzz_encoder ${H}/fuzz_encoder.cpp)
snap_fuzzer(fuzz_stream_uri ${H}/fuzz_stream_uri.cpp)
snap_fuzzer(fuzz_authinfo ${H}/fuzz_authinfo.cpp)
snap_fuzzer(fuzz_properties ${H}/fuzz_properties.cpp)
snap_fuzzer(fuzz_control ${H}/fuzz_control.cpp)
snap_fuzzer(fuzz_stream_session ${H}/fuzz_stream_session.cpp)
snap_fuzzer(fuzz_http ${H}/fuzz_http.cpp)
