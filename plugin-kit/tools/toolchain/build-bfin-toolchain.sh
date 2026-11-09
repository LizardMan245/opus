#!/usr/bin/env bash
# Build the Blackfin toolchain the mod tool uses: GNU binutils + GCC (C only, no C library)
# for target bfin-elf, as native Windows executables.
#
#   wsl -d Ubuntu-22.04 -- bash tools/toolchain/build-bfin-toolchain.sh [step...]   (from the repository's folder)
#
# WSL is only the factory (option B, 2026-09-30): the result in OUT (default: bfin-toolchain beside the repository)
# runs on Windows without WSL.  A Windows-hosted GCC cannot build its own target library
# (libgcc) on Linux, so the toolchain is built twice: first hosted on Linux (the helper),
# then hosted on Windows ("Canadian cross"), whose libgcc is built with the helper.
#
# Steps (default: all, in order): deps fetch verify extract linux win install
# Sources come from ftp.gnu.org and are checked against the GNU keyring signatures; GCC's
# own prerequisites (GMP, MPFR, MPC) come from its download_prerequisites script, which
# checks their SHA-512 sums.  Versions and hashes are written to SOURCES.txt next to the result.
#
# The compiler is built once per computer, never for each plugin: a step whose result is already there (the same
# GCC and binutils) keeps it and says so, and a full run stops at once when OUT already holds the finished compiler.
# FORCE=1 builds again anyway.
set -euo pipefail

BINUTILS=${BINUTILS:-2.38}
GCC=${GCC:-13.3.0}
TARGET=bfin-elf
HOST_WIN=x86_64-w64-mingw32
WORK=${WORK:-/root/bfin-build}
LINUX_PREFIX=$WORK/linux-install
WIN_PREFIX=$WORK/win-install
OUT=${OUT:-$(cd "$(dirname "$0")/../../.." && pwd)/bfin-toolchain}   # beside the repository's folder
JOBS=${JOBS:-$(nproc)}
MIRROR=${MIRROR:-https://ftp.gnu.org/gnu}
FORCE=${FORCE:-0}
BUILT="built-gcc-$GCC-binutils-$BINUTILS"     # the mark a finished build step leaves in its prefix

COMMON=(--target=$TARGET --disable-nls --disable-werror)
GCC_OPTS=(--enable-languages=c --without-headers --with-newlib --disable-shared --disable-threads
          --disable-multilib --disable-libssp --disable-libgomp --disable-libquadmath --disable-libatomic
          --disable-bootstrap --without-isl)

log() { echo "== $(date +%H:%M:%S) $*"; }

kept() {  # kept PREFIX WHAT: true (and says so) when PREFIX already holds this build and FORCE is not 1
    if [ "$FORCE" != 1 ] && [ -e "$1/$BUILT" ]; then
        log "$2 already built in $1 (GCC $GCC, binutils $BINUTILS): kept. FORCE=1 builds it again."
        return 0
    fi
    return 1
}

installed() {  # OUT holds the finished Windows compiler of these versions
    [ -e "$OUT/bin/$TARGET-gcc.exe" ] && grep -q "binutils $BINUTILS, GCC $GCC," "$OUT/SOURCES.txt" 2>/dev/null
}

step_deps() {
    log "apt packages"
    apt-get update -qq
    DEBIAN_FRONTEND=noninteractive apt-get install -y -qq build-essential m4 texinfo bison flex \
        mingw-w64 wget xz-utils bzip2 gnupg >/dev/null
    x86_64-w64-mingw32-gcc --version | head -1
}

step_fetch() {
    mkdir -p "$WORK/dl"
    cd "$WORK/dl"
    for f in binutils/binutils-$BINUTILS.tar.xz gcc/gcc-$GCC/gcc-$GCC.tar.xz; do
        for g in "$f" "$f.sig"; do
            [ -s "$(basename "$g")" ] || { log "download $g"; wget -q "$MIRROR/$g"; }
        done
    done
    [ -s gnu-keyring.gpg ] || { log "download gnu-keyring.gpg"; wget -q "$MIRROR/gnu-keyring.gpg"; }
}

step_verify() {
    cd "$WORK/dl"
    for f in binutils-$BINUTILS.tar.xz gcc-$GCC.tar.xz; do
        log "verify $f"
        gpgv --keyring ./gnu-keyring.gpg "$f.sig" "$f"
    done
}

step_extract() {
    mkdir -p "$WORK/src"
    cd "$WORK/src"
    [ -d binutils-$BINUTILS ] || tar xf "$WORK/dl/binutils-$BINUTILS.tar.xz"
    if [ ! -d gcc-$GCC ]; then
        tar xf "$WORK/dl/gcc-$GCC.tar.xz"
        (cd gcc-$GCC && ./contrib/download_prerequisites --no-graphite)
    fi
    if grep -n 'bfin' gcc-$GCC/gcc/config.gcc | grep -qi obsolete; then
        log "note: GCC $GCC lists bfin as obsolete"
    fi
}

build() {  # build DIR SRCDIR PREFIX [configure args...] -- make targets -- install targets
    local dir=$1 src=$2 prefix=$3; shift 3
    local conf=() mk=() inst=()
    while [ $# -gt 0 ] && [ "$1" != -- ]; do conf+=("$1"); shift; done; shift
    while [ $# -gt 0 ] && [ "$1" != -- ]; do mk+=("$1"); shift; done; shift
    inst=("$@")
    rm -rf "$dir"; mkdir -p "$dir"; cd "$dir"
    log "configure $(basename "$dir")"
    "$src/configure" --prefix="$prefix" "${conf[@]}" > configure.log 2>&1
    log "make $(basename "$dir")"
    make -j"$JOBS" MAKEINFO=true "${mk[@]}" > make.log 2>&1
    make MAKEINFO=true "${inst[@]}" > install.log 2>&1
}

step_linux() {
    if ! kept "$LINUX_PREFIX" "the Linux compiler" && \
       ! { [ "$FORCE" != 1 ] && "$LINUX_PREFIX/bin/$TARGET-gcc" --version 2>/dev/null | head -1 | grep -q " $GCC\$" \
           && log "the Linux compiler already in $LINUX_PREFIX is GCC $GCC: kept. FORCE=1 builds it again."; }; then
        build "$WORK/b-binutils-linux" "$WORK/src/binutils-$BINUTILS" "$LINUX_PREFIX" \
            "${COMMON[@]}" -- all -- install
        export PATH="$LINUX_PREFIX/bin:$PATH"
        build "$WORK/b-gcc-linux" "$WORK/src/gcc-$GCC" "$LINUX_PREFIX" \
            "${COMMON[@]}" "${GCC_OPTS[@]}" -- all-gcc all-target-libgcc -- install-gcc install-target-libgcc
        touch "$LINUX_PREFIX/$BUILT"
    fi
    "$LINUX_PREFIX/bin/$TARGET-gcc" --version | head -1
}

step_win() {
    kept "$WIN_PREFIX" "the Windows compiler" && return 0
    export PATH="$LINUX_PREFIX/bin:$PATH"
    local static=(LDFLAGS="-static -static-libgcc -static-libstdc++")
    build "$WORK/b-binutils-win" "$WORK/src/binutils-$BINUTILS" "$WIN_PREFIX" \
        --host=$HOST_WIN "${COMMON[@]}" "${static[@]}" -- all -- install
    build "$WORK/b-gcc-win" "$WORK/src/gcc-$GCC" "$WIN_PREFIX" \
        --host=$HOST_WIN "${COMMON[@]}" "${GCC_OPTS[@]}" "${static[@]}" \
        -- all-gcc all-target-libgcc -- install-gcc install-target-libgcc
    touch "$WIN_PREFIX/$BUILT"
    ls "$WIN_PREFIX/bin"
}

step_install() {
    if [ "$FORCE" != 1 ] && installed; then
        log "$OUT already holds the compiler (GCC $GCC, binutils $BINUTILS): kept. FORCE=1 replaces it."
        return 0
    fi
    log "copy to $OUT (stripped; without lto-dump, which mods do not use)"
    rm -rf "$OUT"; mkdir -p "$OUT"
    cp -r "$WIN_PREFIX"/. "$OUT"/
    rm -f "$OUT/bin/$TARGET-lto-dump.exe"
    find "$OUT" -name '*.exe' -exec $HOST_WIN-strip {} +
    {
        echo "Blackfin toolchain for Opus, built $(date -u +%Y-%m-%dT%H:%MZ) by tools/toolchain/build-bfin-toolchain.sh"
        echo "binutils $BINUTILS, GCC $GCC, target $TARGET, host $HOST_WIN, C only, no C library, no multilib"
        echo "configure (gcc): ${COMMON[*]} ${GCC_OPTS[*]}"
        echo
        (cd "$WORK/dl" && sha256sum binutils-$BINUTILS.tar.xz gcc-$GCC.tar.xz)
        echo "(both verified with gpgv against gnu-keyring.gpg from $MIRROR)"
    } > "$OUT/SOURCES.txt"
    cat "$OUT/SOURCES.txt"
}

steps=("$@")
if [ ${#steps[@]} -eq 0 ]; then
    if [ "$FORCE" != 1 ] && installed; then
        log "the compiler is already built: $OUT (GCC $GCC, binutils $BINUTILS). Nothing to do: it is built once per"
        log "computer, never for each plugin. Point the kit at it (OP1_BFIN_TOOLCHAIN=$OUT). FORCE=1 builds it again."
        exit 0
    fi
    steps=(deps fetch verify extract linux win install)
fi
for s in "${steps[@]}"; do "step_$s"; done
log "done: ${steps[*]}"
