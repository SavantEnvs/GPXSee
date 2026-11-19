#!/usr/bin/env bash
#
# mayhem/build.sh — build GPXSee's fuzz harnesses and the functional oracle tool.
#
# GPXSee is a Qt GUI app built by qmake (gpxsee.pro, which globs every src/**/*.cpp into one GUI
# binary). The harnesses need only its GUI-free code, so mayhem/fuzz/gpxsee-fuzz.pro builds
# src/common, src/data, src/map (+ two GUI helpers they use, and common.qrc) as a static library,
# with upstream's own sources and Qt (Debian's Qt 6, uninstrumented like any apt library):
#   1) mayhem/build-lib:  that library with $SANITIZER_FLAGS $DEBUG_FLAGS + SanCov → one libFuzzer
#                         binary per format (mayhem/targets.txt) + its -standalone reproducer,
#                         all in mayhem/out/.
#   2) mayhem/build-test: the same library with the project's normal flags → mayhem/out/gpxsee_dump,
#                         the tool mayhem/test.sh runs over the committed sample files.
# Re-runnable (qmake/make are incremental) and offline (Qt comes from apt in the image).
set -euo pipefail

[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${STANDALONE_FUZZ_MAIN:=/opt/mayhem/StandaloneFuzzTargetMain.c}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
: "${SRC:=/mayhem}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"
M="$SRC/mayhem"
QT_MODULES="Qt6Widgets Qt6Gui Qt6Core Qt6Network Qt6Sql Qt6Concurrent Qt6Svg"
QT_CFLAGS="$(pkg-config --cflags $QT_MODULES)"
QT_LIBS="$(pkg-config --libs $QT_MODULES)"
mkdir -p "$M/out"

# qmake_lib <build dir> <compile flags> [extra qmake args...]
qmake_lib() {
  local dir="$1" flags="$2"
  shift 2
  mkdir -p "$dir"
  (cd "$dir" && qmake6 "$M/fuzz/gpxsee-fuzz.pro" CONFIG+=release \
      QMAKE_CC="$CC" QMAKE_CXX="$CXX" QMAKE_LINK="$CXX" \
      "QMAKE_CFLAGS+=$flags" "QMAKE_CXXFLAGS+=$flags" "$@" \
    && make -j"$MAYHEM_JOBS")
}

# 1) Sanitized + SanCov-instrumented library, then the harnesses.
# A qmake release build defines QT_NO_DEBUG, which compiles out every Q_ASSERT — GPXSee's own
# (e.g. the bounds checks in common/rtree.h and map/ozf.cpp) and the ones inside Qt's inline
# containers (QList::at, QString::at, operator[]). QT_FORCE_ASSERTS keeps them on in the
# fuzzed build, so an out-of-range access they guard aborts (qFatal) instead of passing silently.
# Every object of the fuzz binaries (library, harnesses, onmoveparsers_relaxed.o) gets it, except
# the one file below (subfile_mapsforge_relaxed.o); the oracle build (2) keeps the project's normal
# release flags.
FUZZ_FLAGS="$SANITIZER_FLAGS $DEBUG_FLAGS -fsanitize=fuzzer-no-link -DQT_FORCE_ASSERTS"
qmake_lib "$M/build-lib" "$FUZZ_FLAGS" "DEFINES+=QT_FORCE_ASSERTS"
LIB="$M/build-lib/libgpxsee-fuzz.a"

$CXX $SANITIZER_FLAGS $DEBUG_FLAGS -fPIC -c "$M/lsan_off.cc" -o "$M/build-lib/lsan_off.o"
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o "$M/build-lib/standalone_main.o"

# data-any (unknown suffix: every parser in turn, in suffix order) reaches the GHP parser for almost
# every input, and GHPParser::readF0's `time += ms` (src/data/onmoveparsers.cpp) overflows a signed
# int on nearly any bytes — so with full UBSan no input would get past it to the later parsers
# (srt, trk, txt, vkx, wpt, ...). data-any therefore links a copy of that ONE file built without the
# signed-integer-overflow check (it wins over the library's member, which is then never pulled in);
# the data-ghp target keeps the check and exposes that bug. Everything else stays fully sanitized.
$CXX $FUZZ_FLAGS -fno-sanitize=signed-integer-overflow -O2 -std=c++17 -fPIC -I"$SRC/src" $QT_CFLAGS \
  -c "$SRC/src/data/onmoveparsers.cpp" -o "$M/build-lib/onmoveparsers_relaxed.o"

# map-mapsforge: the same for src/map/mapsforge/subfile_mapsforge.cpp, whose one Q_ASSERT,
# `pos < _size` in SubFile::seek, is off by one: MapData::readPaths seeks to the end of the POI
# block, which for a sub-file's last tile with no ways is exactly _size; nothing is read there. Every
# valid map that has such a tile (all three mapsforge reference maps in mayhem/map-mapsforge/testsuite
# do) would abort there before the way decoder runs. map-mapsforge links a copy of that ONE file built
# without QT_FORCE_ASSERTS (it holds no other assert); every other mapsforge file keeps them.
$CXX ${FUZZ_FLAGS/-DQT_FORCE_ASSERTS/-DQT_NO_DEBUG} -O2 -std=c++17 -fPIC -I"$SRC/src" $QT_CFLAGS \
  -c "$SRC/src/map/mapsforge/subfile_mapsforge.cpp" -o "$M/build-lib/subfile_mapsforge_relaxed.o"

while read -r name suffix mode; do
  case "$name" in ''|'#'*) continue ;; esac
  defs="-DSUFFIX=\"$suffix\""
  extra=""
  case "$mode" in
    map) defs="$defs -DMAP_FORMAT" ;;
    any) defs="$defs -DTRY_UNKNOWN"; extra="$M/build-lib/onmoveparsers_relaxed.o" ;;
    ozf) defs="$defs -DOZF_FORMAT" ;;
  esac
  [ "$name" = map-mapsforge ] && extra="$M/build-lib/subfile_mapsforge_relaxed.o"
  obj="$M/build-lib/harness_${name}.o"
  $CXX $FUZZ_FLAGS -O1 -std=c++17 -fPIC -I"$SRC/src" $QT_CFLAGS $defs \
    -c "$M/fuzz/fuzz_gpxsee.cpp" -o "$obj"
  $CXX $SANITIZER_FLAGS $DEBUG_FLAGS $LIB_FUZZING_ENGINE "$obj" $extra "$M/build-lib/lsan_off.o" \
    "$LIB" $QT_LIBS -lz -o "$M/out/$name"
  $CXX $SANITIZER_FLAGS $DEBUG_FLAGS "$obj" $extra "$M/build-lib/standalone_main.o" \
    "$M/build-lib/lsan_off.o" "$LIB" $QT_LIBS -lz -o "$M/out/$name-standalone"
done < "$M/targets.txt"

# 2) The oracle tool, with the project's normal flags (no sanitizers, no SanCov).
qmake_lib "$M/build-test" "$COVERAGE_FLAGS"
$CXX -O2 -std=c++17 -fPIC $COVERAGE_FLAGS -I"$SRC/src" $QT_CFLAGS "$M/test/gpxsee_dump.cpp" \
  "$M/build-test/libgpxsee-fuzz.a" $QT_LIBS -lz -o "$M/out/gpxsee_dump"

echo "build.sh: built $(grep -cv '^\s*\(#\|$\)' "$M/targets.txt") targets in $M/out"
