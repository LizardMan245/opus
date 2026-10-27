#!/usr/bin/env bash
# Build the Blackfin toolchain the mod tool uses: GNU binutils + GCC (C only, no C library)
# for target bfin-elf, as native Windows executables.
#
#   wsl -d Ubuntu-22.04 -- bash /mnt/e/OP1firmware/op1-lab/tools/toolchain/build-bfin-toolchain.sh [step...]
#
# WSL is only the factory (option B, 2026-09-30): the result in E:\OP1firmware\bfin-toolchain
# runs on Windows without WSL.  A Windows-hosted GCC cannot build its own target library
# (libgcc) on Linux, so the toolchain is built twice: first hosted on Linux (the helper),
# then hosted on Windows ("Canadian cross"), whose libgcc is built with the helper.
#
# Steps (default: all, in order): deps fetch verify extract linux win install
# Sources come from ftp.gnu.org and are checked against the GNU keyring signatures; GCC's
# own prerequisites (GMP, MPFR, MPC) come from its download_prerequisites script, which
# checks their SHA-512 sums.  Versions and hashes are written to SOURCES.txt next to the result.
set -euo pipefail

BINUTILS=${BINUTILS:-2.38}
GCC=${GCC:-13.3.0}
TARGET=bfin-elf
HOST_WIN=x86_64-w64-mingw32
WORK=${WORK:-/root/bfin-build}
LINUX_PREFIX=$WORK/linux-install
WIN_PREFIX=$WORK/win-install
OUT=${OUT:-/mnt/e/OP1firmware/bfin-toolchain}
JOBS=${JOBS:-$(nproc)}
MIRROR=${MIRROR:-https://ftp.gnu.org/gnu}

COMMON=(--target=$TARGET --disable-nls --disable-werror)
GCC_OPTS=(--enable-languages=c --without-headers --with-newlib --disable-shared --disable-threads
          --disable-multilib --disable-libssp --disable-libgomp --disable-libquadmath --disable-libatomic
          --disable-bootstrap --without-isl)

log() { echo "== $(date +%H:%M:%S) $*"; }

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
    build "$WORK/b-binutils-linux" "$WORK/src/binutils-$BINUTILS" "$LINUX_PREFIX" \
        "${COMMON[@]}" -- all -- install
    export PATH="$LINUX_PREFIX/bin:$PATH"
    build "$WORK/b-gcc-linux" "$WORK/src/gcc-$GCC" "$LINUX_PREFIX" \
        "${COMMON[@]}" "${GCC_OPTS[@]}" -- all-gcc all-target-libgcc -- install-gcc install-target-libgcc
    "$LINUX_PREFIX/bin/$TARGET-gcc" --version | head -1
}

step_win() {
    export PATH="$LINUX_PREFIX/bin:$PATH"
    local static=(LDFLAGS="-static -static-libgcc -static-libstdc++")
    build "$WORK/b-binutils-win" "$WORK/src/binutils-$BINUTILS" "$WIN_PREFIX" \
        --host=$HOST_WIN "${COMMON[@]}" "${static[@]}" -- all -- install
    build "$WORK/b-gcc-win" "$WORK/src/gcc-$GCC" "$WIN_PREFIX" \
        --host=$HOST_WIN "${COMMON[@]}" "${GCC_OPTS[@]}" "${static[@]}" \
        -- all-gcc all-target-libgcc -- install-gcc install-target-libgcc
    ls "$WIN_PREFIX/bin"
}

step_install() {
    log "copy to $OUT (stripped; without lto-dump, which mods do not use)"
    rm -rf "$OUT"; mkdir -p "$OUT"
    cp -r "$WIN_PREFIX"/. "$OUT"/
    rm -f "$OUT/bin/$TARGET-lto-dump.exe"
    find "$OUT" -name '*.exe' -exec $HOST_WIN-strip {} +
    {
        echo "Blackfin toolchain for op1-lab, built $(date -u +%Y-%m-%dT%H:%MZ) by tools/toolchain/build-bfin-toolchain.sh"
        echo "binutils $BINUTILS, GCC $GCC, target $TARGET, host $HOST_WIN, C only, no C library, no multilib"
        echo "configure (gcc): ${COMMON[*]} ${GCC_OPTS[*]}"
        echo
        (cd "$WORK/dl" && sha256sum binutils-$BINUTILS.tar.xz gcc-$GCC.tar.xz)
        echo "(both verified with gpgv against gnu-keyring.gpg from $MIRROR)"
    } > "$OUT/SOURCES.txt"
    cat "$OUT/SOURCES.txt"
}

steps=("$@")
[ ${#steps[@]} -gt 0 ] || steps=(deps fetch verify extract linux win install)
for s in "${steps[@]}"; do "step_$s"; done
log "done: ${steps[*]}"
