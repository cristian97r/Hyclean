#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

cd /src
mkdir -p "$HOME"
ln -s /cache .cache  # CPM uses <source>/.cache/cpm.
printf '%s\n' "${GIT_COMMIT:?}" > GIT-COMMIT
printf '%s\n' "${GIT_REFSPEC:?}" > GIT-REFSPEC

time cmake -S . -B build/release -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DLICHII_TESTS=OFF \
    -DLICHII_CMD=ON -DLICHII_ROOM=OFF -DLICHII_ROOM_STANDALONE=OFF \
    -DLICHII_USE_BUNDLED_QT=OFF -DENABLE_QT_TRANSLATION=ON "-DCMAKE_PREFIX_PATH=$QT_ROOT_DIR" \
    -DLICHII_USE_BUNDLED_SDL3=ON -DVulkanHeaders_FORCE_BUNDLED=ON -DVulkanUtilityLibraries_FORCE_BUNDLED=ON \
    -DENABLE_UPDATE_CHECKER=ON "-DLICHII_BUILD_TAG=${BUILD_TAG:?}" -DLICHII_BUILD_PLATFORM=linux-x86_64 \
    "-DLICHII_UPDATE_REPO=${UPDATE_REPO:?}" -DUSE_DISCORD_PRESENCE=OFF -DUSE_CCACHE=ON
ccache --zero-stats
time cmake --build build/release --target lichii lichii-cmd
ccache --show-stats
export FSR4_BUILD_JOBS="$CMAKE_BUILD_PARALLEL_LEVEL"
time bash docker/scripts/package-linux-release.sh
