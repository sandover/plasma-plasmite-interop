#!/usr/bin/env bash
# Build and check a portable archive on the target operating system and CPU.
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
out=${1:?Usage: scripts/build-release.sh OUTPUT_DIRECTORY}
mkdir -p "$out"
out=$(cd "$out" && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
prefix=$work/install
jobs=$(getconf _NPROCESSORS_ONLN)

case $(uname -s) in
    Darwin) platform=macos; ssl_dir=/etc/ssl; export MACOSX_DEPLOYMENT_TARGET=13.0 ;;
    Linux) platform=linux; ssl_dir=/etc/ssl ;;
    *) echo 'Build on macOS or Linux.' >&2; exit 1 ;;
esac
case $(uname -m) in
    arm64|aarch64) arch=arm64 ;;
    x86_64) arch=x86_64 ;;
    *) echo 'Build on ARM64 or x86-64.' >&2; exit 1 ;;
esac
name=plasticine-$platform-$arch

# Pin dependencies so a release does not change when an upstream branch moves.
plasma=42df00a152aeaad194a0fe86197373e51f63976e
yaml=2c891fc7a770e8ba2fec34fc6b545c672beb37e6
openssl=45e844fa2a14ec92d146bd8f5778ac130b6625fb # OpenSSL 3.5.9
fetch() {
    local repo=$1 commit=$2 dest=$3
    mkdir -p "$dest"
    curl --fail --location --retry 3 "https://codeload.github.com/$repo/tar.gz/$commit" |
        tar -xz --strip-components=1 -C "$dest"
}
fetch yaml/libyaml "$yaml" "$work/yaml"
fetch openssl/openssl "$openssl" "$work/openssl"
fetch plasma-hamper/plasma "$plasma" "$work/plasma"

cmake -S "$work/yaml" -B "$work/yaml-build" \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$prefix" -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTING=OFF
cmake --build "$work/yaml-build" --target yaml --parallel "$jobs"
cmake --install "$work/yaml-build"
(
    cd "$work/openssl"
    ./Configure no-shared no-tests no-module --prefix="$prefix" --libdir=lib --openssldir="$ssl_dir"
    make -j"$jobs" build_libs
    make install_dev
)

export PKG_CONFIG_PATH=$prefix/lib/pkgconfig
cmake -S "$work/plasma" -B "$work/plasma-build" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local \
    -DCMAKE_PREFIX_PATH="$prefix" -DCMAKE_C_FLAGS="-I$prefix/include" \
    -DBUILD_SHARED_LIBS=OFF -DBUILD_CXX_LIBS=OFF -DBUILD_TESTS=OFF
cmake --build "$work/plasma-build" --target Plasma --parallel "$jobs"
cp "$work/plasma-build/libPlasma/c/libPlasma.a" \
    "$work/plasma-build/libLoam/c/libLoam.a" "$prefix/lib/"
for library in libPlasma libLoam; do
    sed "s|/usr/local|$prefix|g" "$work/plasma-build/lib/pkgconfig/$library.pc" > "$prefix/lib/pkgconfig/$library.pc"
done

# Build in scratch space; keep the developer's current executables untouched.
mkdir "$work/bridge"
cp "$root/"{Makefile,plasticine.c,interop.c,interop.h,wire.c,tests.c} "$work/bridge/"
cp -R "$root/vendor" "$work/bridge/"
make -C "$work/bridge" all test \
    CPPFLAGS="-D_POSIX_C_SOURCE=200809L -I$work/plasma -I$work/plasma-build $(pkg-config --cflags libPlasma)" \
    CFLAGS='-O2 -Wall -Wextra -Werror -std=c11' \
    LDLIBS="$(pkg-config --static --libs libPlasma)"
binary=$work/bridge/plasticine

# Fail if the executable needs any separately installed libraries.
if [[ $platform == macos ]]; then
    otool -L "$binary"
    otool -L "$binary" | tail -n +2 | awk '{print $1}' |
        while read -r library; do
            case $library in /usr/lib/*|/System/Library/*) ;; *) exit 1 ;; esac
        done
    codesign --force --sign - "$binary"
else
    readelf -d "$binary"
    readelf -d "$binary" | sed -n 's/.*Shared library: \[\(.*\)\]/\1/p' |
        while read -r library; do
            case $library in libc.so.*|libm.so.*|libpthread.so.*|libdl.so.*|librt.so.*|ld-linux*.so.*) ;; *) exit 1 ;; esac
        done
fi

package=$work/$name
mkdir -p "$package/licenses"
cp "$binary" "$root/README.md" "$root/LICENSE" "$package/"
cp "$work/plasma/LICENSE" "$package/licenses/Plasma-LICENSE"
cp "$work/plasma/COPYING" "$package/licenses/Plasma-COPYING"
cp "$work/yaml/License" "$package/licenses/libyaml-LICENSE"
cp "$work/openssl/LICENSE.txt" "$package/licenses/OpenSSL-LICENSE"
cp "$root/vendor/yyjson/LICENSE" "$package/licenses/yyjson-LICENSE"
printf 'Plasma %s\nlibyaml %s\nOpenSSL %s\nyyjson 0.12.0\n' \
    "$plasma" "$yaml" "$openssl" > "$package/dependencies.txt"
tar -czf "$out/$name.tar.gz" -C "$work" "$name"
mkdir "$work/unpacked"
tar -xzf "$out/$name.tar.gz" -C "$work/unpacked"
"$work/unpacked/$name/plasticine" --help
(cd "$out" && shasum -a 256 "$name.tar.gz" > "$name.tar.gz.sha256")
