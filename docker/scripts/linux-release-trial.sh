#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

# Run from repository root. Host owns both mounts; no root-owned cache files.
TRIAL_UID=$(id -u)
TRIAL_GID=$(id -g)
export TRIAL_UID TRIAL_GID
GIT_COMMIT=$(git rev-parse HEAD)
GIT_REFSPEC=${GITHUB_REF_NAME:-$(git rev-parse --abbrev-ref HEAD)}
export GIT_COMMIT GIT_REFSPEC
export BUILD_TAG="build-${GITHUB_RUN_NUMBER:-0}" UPDATE_REPO="${GITHUB_REPOSITORY:-tasarren/Lichii}"
if [[ -z ${TRIAL_FSR4_CACHE:-} ]]; then
    fsr4_key=$(sha256sum Dockerfile tools/fsr4-vk/build.sh | sha256sum)
    export TRIAL_FSR4_CACHE="$PWD/.cache/linux-release-trial-fsr4/${fsr4_key%% *}"
fi
mkdir -p .cache/linux-release-trial builds/linux-release-trial "$TRIAL_FSR4_CACHE"

# Use Noble's own apt sources, not the hosted runner's regional mirror.
echo 'Build trial image (Ubuntu packages, CMake, Qt)'
time docker compose --progress plain -p lichii-release-trial build linux-release-trial
exec docker compose -p lichii-release-trial run --rm --pull never --no-deps linux-release-trial
