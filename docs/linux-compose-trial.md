# Linux x86_64 release trial

From a clean checkout of `ci/compose-release` on an x86_64 Linux host with Docker Compose:

```sh
bash docker/scripts/linux-release-trial.sh
```

This builds the standard GCC release on Ubuntu 24.04. It creates a SHA-named AppImage and tar.gz in `builds/linux-release-trial/`. The two programs use the checkout's commit and branch. Outside Actions, the update checker uses `build-0` and `tasarren/Lichii`. To reproduce a hosted build's metadata, use the same checkout and set `GITHUB_RUN_NUMBER`, `GITHUB_REPOSITORY`, and `GITHUB_REF_NAME` to that run's values. `BUILD_JOBS` defaults to four.

Each run configures a fresh build directory inside the container. CPM downloads and compiler objects survive in `.cache/linux-release-trial/`. FSR4's library and licenses survive in `.cache/linux-release-trial-fsr4/`, keyed by `Dockerfile` and the pinned FSR4 build script. Hosted runs restore both from separate trial-only Actions caches. The image is built and timed separately using the Ubuntu 24.04 image's apt sources. Hosted runs do not cache the image or apt archives.

Manual `build-check` dispatch on `ci/compose-release` runs only this job. It uploads test artifacts without creating a tag or release. Other refs retain their normal build checks. Production release jobs and Windows builds do not use this recipe.

For the warm-cache measurement, dispatch with `seed_linux_fsr4=true` to restore the matching release FSR4 cache if the trial has no copy. This reads the release cache but saves only to the trial cache. Leave the input off for normal use.
