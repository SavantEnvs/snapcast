#!/usr/bin/env bash
#
# mayhem/build.sh — build snapcast's fuzz harnesses and its own test suite.
#
# Runs inside the commit image (mayhem/Dockerfile) as `mayhem` in /mayhem; the base exports CC/CXX,
# SANITIZER_FLAGS, LIB_FUZZING_ENGINE, STANDALONE_FUZZ_MAIN and SRC. Re-runnable and offline: every
# dependency is an apt -dev package installed by the Dockerfile (Catch2 too, so test/CMakeLists.txt
# never reaches its FetchContent fallback).
#
#  1) build/tests: upstream's own CMake build with BUILD_TESTS=ON and the project's normal flags
#     (its snapcast_test and snapserver targets) -> bin/snapcast_test (Catch2) and bin/snapserver, which
#     mayhem/test.sh runs (the Catch2 suite, then upstream's test/stream_removal_test.py against snapserver).
#  2) build/fuzz: mayhem/CMakeLists.txt compiles common/, client/decoder/ and server/ with
#     SANITIZER_FLAGS + DEBUG_FLAGS + SanCov and links each harness in mayhem/harnesses/ twice:
#     build/fuzz/<name> (libFuzzer) and build/fuzz/<name>-standalone (run-once reproducer), each with
#     mayhem/lsan_off.cc linked in (LeakSanitizer off; ASan/UBSan stay on).
# Both build directories are under build/, which upstream's .gitignore already ignores.
set -euo pipefail

[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${STANDALONE_FUZZ_MAIN:=/opt/mayhem/StandaloneFuzzTargetMain.c}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
: "${SRC:=/mayhem}"
export CC CXX

cd "$SRC"

# 1) The upstream test suite, normal flags (an honest functional oracle, no sanitizers).
cmake -S . -B build/tests -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTS=ON -DBUILD_SERVER=ON -DBUILD_CLIENT=OFF \
  -DBUILD_WITH_AVAHI=OFF -DBUILD_WITH_ALSA=OFF \
  -DCMAKE_C_FLAGS="$COVERAGE_FLAGS" -DCMAKE_CXX_FLAGS="$COVERAGE_FLAGS" \
  -DCMAKE_EXE_LINKER_FLAGS="$COVERAGE_FLAGS"
cmake --build build/tests -j"$MAYHEM_JOBS" --target snapcast_test snapserver

# 2) The fuzz harnesses. The standalone driver is C; compile it as C so its LLVMFuzzerTestOneInput
#    reference keeps C linkage.
mkdir -p build/fuzz
# The project objects carry SanCov (-fsanitize=fuzzer-no-link) even when SANITIZER_FLAGS is empty; the
# ASan/UBSan runtime provides the SanCov callbacks to the standalone reproducers, so without a sanitizer
# runtime they link libFuzzer's no-main runtime for them instead.
standalone_extra=""
case " $SANITIZER_FLAGS " in
  *-fsanitize=*) ;;
  *) standalone_extra="$("$CXX" -print-file-name=libclang_rt.fuzzer_no_main-x86_64.a)" ;;
esac
# shellcheck disable=SC2086
"$CC" $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o build/fuzz/standalone_main.o
cmake -S mayhem -B build/fuzz -G Ninja \
  -DCMAKE_BUILD_TYPE=None \
  -DCMAKE_C_FLAGS="$SANITIZER_FLAGS $DEBUG_FLAGS" \
  -DCMAKE_CXX_FLAGS="$SANITIZER_FLAGS $DEBUG_FLAGS" \
  -DCMAKE_EXE_LINKER_FLAGS="$SANITIZER_FLAGS" \
  -DLIB_FUZZING_ENGINE="$LIB_FUZZING_ENGINE" \
  -DSTANDALONE_MAIN_OBJ="$SRC/build/fuzz/standalone_main.o" \
  -DSTANDALONE_EXTRA="$standalone_extra" \
  -DLSAN_OFF_SRC="$SRC/mayhem/lsan_off.cc"
cmake --build build/fuzz -j"$MAYHEM_JOBS"

echo "built:"; ls build/fuzz | grep '^fuzz_'
