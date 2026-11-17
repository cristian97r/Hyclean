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
mkdir -p .cache/linux-release-trial builds/linux-release-trial
exec docker compose -p lichii-release-trial run --rm --build --no-deps linux-release-trial
