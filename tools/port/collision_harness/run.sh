#!/bin/bash
# Build and run the collision harness, then compare its output with expected.txt.
#   run.sh            host build with ${CC:-clang}
#   run.sh --android  static aarch64 Android build (needs an NDK and qemu-aarch64)
# Exit code 0 = output matches.
set -e
cd "$(dirname "$0")"
ROOT=$(cd ../../.. && pwd)
MODE=${1:-host}
OUT=$(mktemp -d "${TMPDIR:-/tmp}/collision_harness.XXXXXX")
trap 'rm -rf "$OUT"' EXIT

# Same defines and code-generation flags as the game target in CMakeLists.txt.
FLAGS="-DPORT=1 -DF3DEX_GBI_2=1 -DGBI_FLOATS=1 -DVERSION_US=1 -D_LANGUAGE_C -DAVOID_UB=1 -DNO_OTR_TEXTURES=1
  -I$ROOT -I$ROOT/include -I$ROOT/src -I$ROOT/port -I$ROOT/libultraship/include
  -I$ROOT/libultraship/include/libultraship -I$ROOT/libultraship/src
  -O1 -g -DNDEBUG -std=gnu11 -fno-strict-aliasing -fwrapv -fsigned-char -w"
SRCS="harness.c $ROOT/src/collision.c $ROOT/src/43F0.c $ROOT/port/testing_bridge.c"

case "$MODE" in
  host|--host)
    CC_CMD="${CC:-clang}"
    RUN=""
    ;;
  --android|android)
    NDK=${ANDROID_NDK_HOME:-${ANDROID_NDK_LATEST_HOME:-$(ls -d /opt/android-sdk/ndk/* ${ANDROID_HOME:-/nonexistent}/ndk/* 2>/dev/null | sort -V | tail -1)}}
    TOOLCHAIN="$NDK/toolchains/llvm/prebuilt/linux-x86_64"
    [ -x "$TOOLCHAIN/bin/clang" ] || { echo "run.sh: NDK clang not found under $NDK" >&2; exit 2; }
    command -v qemu-aarch64 > /dev/null || { echo "run.sh: qemu-aarch64 not found (apt-get install qemu-user)" >&2; exit 2; }
    # Static so qemu-user can run it without an Android sysroot; real ELF TLS because
    # Bionic's static libc needs a 64-byte aligned TLS segment (see tls_pad in harness.c).
    CC_CMD="$TOOLCHAIN/bin/clang --target=aarch64-none-linux-android24 --sysroot=$TOOLCHAIN/sysroot -static -fno-emulated-tls"
    RUN="qemu-aarch64"
    ;;
  *)
    echo "usage: run.sh [--host|--android]" >&2; exit 2 ;;
esac

# The game sources reference symbols the harness does not need (evt, heaps, models);
# generate empty stubs for whatever the linker reports missing.
STUBS="$OUT/stubs.c"
: > "$STUBS"
for attempt in 1 2 3 4; do
  if $CC_CMD $FLAGS $SRCS "$STUBS" -o "$OUT/harness" -lm 2> "$OUT/link.log"; then
    break
  fi
  grep -o "undefined reference to \`[^']*'\|undefined symbol: [A-Za-z_][A-Za-z0-9_]*" "$OUT/link.log" \
    | sed "s/.*\`//; s/'//; s/undefined symbol: //" | sort -u > "$OUT/undef.txt"
  if [ ! -s "$OUT/undef.txt" ] || [ "$attempt" = 4 ]; then
    cat "$OUT/link.log" >&2
    echo "run.sh: build failed" >&2
    exit 1
  fi
  while read -r sym; do echo "void $sym(void) {}" >> "$STUBS"; done < "$OUT/undef.txt"
done

$RUN "$OUT/harness" > "$OUT/actual.txt" 2>&1
if diff -u expected.txt "$OUT/actual.txt" > "$OUT/diff.txt"; then
  echo "collision harness ($MODE): output matches expected.txt"
else
  cat "$OUT/diff.txt"
  echo "collision harness ($MODE): OUTPUT DIFFERS from expected.txt" >&2
  exit 1
fi
