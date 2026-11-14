#!/bin/sh
# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu

arch=$(uname -m)
platform=${BUILD_PLATFORM:-linux-${arch}}
out="/artifacts/${platform}"

mkdir -p "${out}" /opt/linuxdeploy
strip build/bin/lichii build/bin/eden-cli

curl -fsSL -o /opt/linuxdeploy/linuxdeploy.AppImage \
    "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-${arch}.AppImage"
curl -fsSL -o /opt/linuxdeploy/linuxdeploy-plugin-qt.AppImage \
    "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-${arch}.AppImage"
chmod +x /opt/linuxdeploy/*.AppImage

export APPIMAGE_EXTRACT_AND_RUN=1
export LDAI_VERSION="${BUILD_TAG:-build-local}"
export LDAI_OUTPUT="lichii-${platform}.AppImage"
export OUTPUT="${LDAI_OUTPUT}"
export ARCH="${arch}"
if command -v qmake6 >/dev/null 2>&1; then
    export QMAKE="$(command -v qmake6)"
fi

mkdir -p AppDir/usr/bin AppDir/usr/share/licenses
cp build/bin/nx2_switches.txt AppDir/usr/bin/

# The desktop file names this icon, so linuxdeploy must see it under that name.
icon="/tmp/io.github.zortos293.lichii.png"
cp dist/lichii.png "${icon}"

/opt/linuxdeploy/linuxdeploy.AppImage --appdir AppDir \
    --executable build/bin/lichii \
    --executable build/bin/eden-cli \
    --desktop-file dist/io.github.zortos293.lichii.desktop \
    --icon-file "${icon}" \
    --plugin qt \
    --output appimage

test -s "${LDAI_OUTPUT}"
cp "${LDAI_OUTPUT}" "${out}/"

mv AppDir lichii
ln -s usr/bin/lichii lichii/lichii
ln -s usr/bin/eden-cli lichii/eden-cli
tar -czf "${out}/lichii-${platform}.tar.gz" lichii

cd "${out}"
sha256sum * > SHA256SUMS
