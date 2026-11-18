#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

fail() { echo "package-linux-release: $*" >&2; exit 1; }

[[ $PWD == /src && $(uname -m) == x86_64 ]] || fail 'expected /src on x86_64'
[[ ${GIT_COMMIT:-} =~ ^[0-9a-f]{40}$ ]] || fail 'GIT_COMMIT must be a full commit SHA'
[[ ${BUILD_TAG:-} =~ ^build-[0-9]+$ ]] || fail 'BUILD_TAG must be build-N'
[[ ${QT_ROOT_DIR:-} == /opt/Qt/6.8.3/gcc_64 ]] || fail 'expected Qt 6.8.3 gcc_64'
export QMAKE="$QT_ROOT_DIR/bin/qmake"
[[ $("$QMAKE" -query QT_VERSION) == 6.8.3 ]] || fail 'qmake does not report Qt 6.8.3'
[[ $(getconf GNU_LIBC_VERSION) == 'glibc 2.39' ]] || fail 'expected Ubuntu 24.04 glibc 2.39'

bin=$PWD/build/release/bin
dist=$PWD/dist
test -x "$bin/lichii"
test -x "$bin/eden-cli"
test -s "$bin/nx2_switches.txt"
strip "$bin/lichii" "$bin/eden-cli"

work=$(mktemp -d /tmp/lichii-package.XXXXXXXX)
trap 'rm -rf -- "$work"' EXIT
fsr4=amd_fidelityfx_upscaler_vk.so
fsr4_output=/fsr4-cache
if [[ -s "$fsr4_output/$fsr4" && -s "$fsr4_output/licenses/fsr4-vk/LICENSE" &&
      -s "$fsr4_output/licenses/fsr4-vk/AMD-FidelityFX-SDK-license.md" ]]; then
    echo 'Use cached FSR4 output'
else
    fsr4_output="$work/fsr4-vk"
    echo 'Build FSR4 output'
    time bash tools/fsr4-vk/build.sh "$work/fsr4-vk-work" "$fsr4_output"
    test -s "$fsr4_output/licenses/fsr4-vk/LICENSE"
    test -s "$fsr4_output/licenses/fsr4-vk/AMD-FidelityFX-SDK-license.md"
fi

cd "$work"
for tool in linuxdeploy linuxdeploy-plugin-qt; do
    image="$tool-x86_64.AppImage"
    curl -fsSL --retry 3 -o "$image" "https://github.com/linuxdeploy/$tool/releases/download/continuous/$image"
done
printf '%s\n' \
    'df908b19f1ed1cdafa79508d415aa12d6c56ce4d56d4149aa8c09286718706e5  linuxdeploy-x86_64.AppImage' \
    'cfc1055b2b9dbc08412b579f20990b7b41a17b61beaa5847dc9477c96c9e9617  linuxdeploy-plugin-qt-x86_64.AppImage' |
    sha256sum --check --strict
chmod +x linuxdeploy{,-plugin-qt}-x86_64.AppImage

name="lichii-$GIT_COMMIT-linux-x86_64"
export ARCH=x86_64 APPIMAGE_EXTRACT_AND_RUN=1
export EXTRA_QT_MODULES=waylandcompositor
export EXTRA_PLATFORM_PLUGINS='libqwayland-egl.so;libqwayland-generic.so'
export LDAI_VERSION="$BUILD_TAG-${GIT_COMMIT::7}"
export LDAI_OUTPUT="$name.AppImage" OUTPUT="$name.AppImage"
icon=io.github.zortos293.lichii.png
cp "$dist/lichii.png" "$icon"
mkdir -p AppDir/usr/bin AppDir/usr/share/licenses
cp "$bin/nx2_switches.txt" AppDir/usr/bin/
cp -r "$fsr4_output/licenses/fsr4-vk" AppDir/usr/share/licenses/
./linuxdeploy-x86_64.AppImage --appdir AppDir \
    --executable "$bin/lichii" --executable "$bin/eden-cli" \
    --library "$fsr4_output/$fsr4" --exclude-library 'libvulkan.so*' \
    --desktop-file "$dist/io.github.zortos293.lichii.desktop" --icon-file "$work/$icon" \
    --plugin qt --output appimage

mv AppDir lichii
ln -s usr/bin/lichii lichii/lichii
ln -s usr/bin/eden-cli lichii/eden-cli
tar -czf "$name.tar.gz" lichii
tar -dzf "$name.tar.gz"

# Inspect the actual AppImage without FUSE. Both formats must ship the same programs and libraries.
"./$LDAI_OUTPUT" --appimage-extract >/dev/null
test -x squashfs-root/AppRun
diff -qr --no-dereference lichii/usr/bin squashfs-root/usr/bin
diff -qr --no-dereference lichii/usr/lib squashfs-root/usr/lib
for root in lichii squashfs-root; do
    test -s "$root/usr/bin/qt.conf"
    cmp "$bin/nx2_switches.txt" "$root/usr/bin/nx2_switches.txt"
    diff -qr "$fsr4_output/licenses/fsr4-vk" "$root/usr/share/licenses/fsr4-vk"
    for plugin in libqwayland-egl.so libqwayland-generic.so; do
        [[ -n $(find "$root/usr" -path "*/platforms/$plugin" -print -quit) ]] || fail "$root lacks $plugin"
    done
    [[ -z $(find "$root" -name 'libvulkan.so*' -print -quit) ]] || fail "$root bundles the Vulkan loader"
done
nm -D --defined-only "lichii/usr/lib/$fsr4" > fsr4-symbols.txt
for symbol in ffxCreateContext ffxDestroyContext ffxQuery ffxDispatch; do
    grep -w "$symbol" fsr4-symbols.txt
done

# Check dependency resolution without the build's Qt environment; the CLI version path needs no GPU.
for elf in lichii/usr/bin/{lichii,eden-cli} "lichii/usr/lib/$fsr4"; do
    env -i PATH=/usr/bin:/bin ldd "$elf" > dependencies.txt
    if grep -E 'not found|=> /(opt/Qt|src)/|libQt6[^ ]* => /(usr/)?lib/' dependencies.txt; then
        fail "$elf has missing dependencies or resolves against external Qt or the build tree"
    fi
done
mkdir -p smoke-home/{.config,.cache,.local/share}
env -i HOME="$work/smoke-home" PATH=/usr/bin:/bin timeout 30s ./lichii/eden-cli --version

if [[ $fsr4_output != /fsr4-cache ]]; then
    cp -a "$fsr4_output/." /fsr4-cache/
fi
mkdir -p /out
mv "$LDAI_OUTPUT" "$name.tar.gz" /out/
sha256sum "/out/$LDAI_OUTPUT" "/out/$name.tar.gz"
