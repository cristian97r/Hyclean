# syntax=docker/dockerfile:1.7
# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Build Lichii without the host toolchain.
#
# Linux:
#   docker compose up --build --abort-on-container-exit linux-amd64
#   docker compose up --build --abort-on-container-exit linux-arm64   (needs an arm64 host or binfmt/QEMU)
#
# Windows, cross-compiled from Linux:
#   docker compose up --build --abort-on-container-exit windows-amd64
#
# Artifacts land in ./builds/<platform>.

ARG DEBIAN_VERSION=trixie-slim

# ---------------------------------------------------------------------------
# Shared Linux build environment.
# ---------------------------------------------------------------------------
FROM debian:${DEBIAN_VERSION} AS linux-base
ARG DEBIAN_FRONTEND=noninteractive
ARG TARGETARCH

ENV LANG=C.UTF-8
ENV CCACHE_DIR=/src/.ccache

RUN apt-get update -o Acquire::AllowInsecureRepositories=true \
    && apt-get install -y --no-install-recommends debian-archive-keyring \
    && apt-get update \
    && apt-get install -y --no-install-recommends \
        autoconf ca-certificates ccache cmake curl desktop-file-utils file g++ gcc git \
        glslang-tools libasound2-dev libavcodec-dev libavfilter-dev libboost-context-dev \
        libboost-dev libboost-fiber-dev libglu1-mesa-dev libopus-dev libpulse-dev \
        libssl-dev libswscale-dev libtool libudev-dev libusb-1.0-0-dev libva-dev \
        libvdpau-dev libvulkan-dev libx11-dev libxcb-cursor0 libxcb-icccm4 \
        libxcb-image0 libxcb-keysyms1 libxcb-randr0 libxcb-render-util0 libxcb-shape0 \
        libxcb-xinerama0 libxcb-xkb1 libxext-dev libxkbcommon-dev libxkbcommon-x11-0 \
        nasm ninja-build patch pkg-config python3 qt6-base-private-dev qt6-charts-dev \
        qt6-tools-dev spirv-headers spirv-tools unzip zip zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

COPY docker/scripts /usr/local/bin/
RUN chmod +x /usr/local/bin/*.sh

# ---------------------------------------------------------------------------
# Windows cross build, using clang-cl + MSVC CRT/SDK from xwin + Windows Qt.
# No Windows container is involved.
# ---------------------------------------------------------------------------
FROM linux-base AS windows-cross-base
ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends clang lld llvm python3-pip wine \
    && rm -rf /var/lib/apt/lists/*
RUN ln -sf /usr/lib/llvm-19/bin/clang /usr/local/bin/clang-cl

RUN set -eux; \
    xwin_version=0.10.0; \
    xwin_prefix="xwin-${xwin_version}-x86_64-unknown-linux-musl"; \
    curl -fsSL -o /tmp/xwin.tar.gz \
        "https://github.com/Jake-Shadle/xwin/releases/download/${xwin_version}/${xwin_prefix}.tar.gz"; \
    echo "d870eb4b2f390878af6da1ccd3cf321d22fcb72720984853b4be732ae597fc88  /tmp/xwin.tar.gz" \
        | sha256sum -c -; \
    tar -xzf /tmp/xwin.tar.gz -C /usr/local/bin --strip-components=1 "${xwin_prefix}/xwin"; \
    rm /tmp/xwin.tar.gz; \
    XWIN_ACCEPT_LICENSE=true xwin splat --output /xwin

RUN python3 -m pip install --break-system-packages --no-cache-dir aqtinstall
RUN aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -m qtcharts -O /opt/Qt-win
# Host tools must match the target Qt version. Debian's Qt 6.8.2 has no
# Qt6HostInfo layout under QT_HOST_PATH=/usr, so CMake fell back to moc.exe.
RUN aqt install-qt linux desktop 6.8.3 linux_gcc_64 -O /opt/Qt-host

ENV WINEPREFIX=/wine
ENV WINEDEBUG=-all
ENV WINEDLLOVERRIDES=mscoree,mshtml=
RUN wineboot -u >/dev/null 2>&1 || true

FROM windows-cross-base AS windows-cross-build
ARG GIT_COMMIT=local
ARG GIT_REFSPEC=nx2
ARG BUILD_TAG=build-local
ARG BUILD_TYPE=Release
ARG ENABLE_WERROR=OFF
ARG ENABLE_OPENGL=ON
ARG ENABLE_LIBUSB=ON
ARG ENABLE_CUBEB=ON
ARG ENABLE_RESHADE=ON
ARG ENABLE_LSFG=ON
ARG ENABLE_UPDATE_CHECKER=OFF
ARG ENABLE_WEB_SERVICE=OFF
ARG ENABLE_QT_TRANSLATION=OFF

ARG BUILD_JOBS=24
ARG BUILD_NICE=0
ENV CMAKE_BUILD_PARALLEL_LEVEL=${BUILD_JOBS} BUILD_NICE=${BUILD_NICE}
ENV GIT_COMMIT=${GIT_COMMIT}
ENV GIT_REFSPEC=${GIT_REFSPEC}
ENV BUILD_TAG=${BUILD_TAG}
ENV BUILD_TYPE=${BUILD_TYPE}
ENV ENABLE_WERROR=${ENABLE_WERROR}
ENV ENABLE_OPENGL=${ENABLE_OPENGL}
ENV ENABLE_LIBUSB=${ENABLE_LIBUSB}
ENV ENABLE_CUBEB=${ENABLE_CUBEB}
ENV ENABLE_RESHADE=${ENABLE_RESHADE}
ENV ENABLE_LSFG=${ENABLE_LSFG}
ENV ENABLE_UPDATE_CHECKER=${ENABLE_UPDATE_CHECKER}
ENV ENABLE_WEB_SERVICE=${ENABLE_WEB_SERVICE}
ENV ENABLE_QT_TRANSLATION=${ENABLE_QT_TRANSLATION}

WORKDIR /src
COPY . .
RUN printf '%s\n' "$GIT_REFSPEC" > GIT-REFSPEC \
    && printf '%s\n' "$GIT_COMMIT" > GIT-COMMIT
RUN --mount=type=cache,target=/src/.cache/cpm \
    --mount=type=cache,target=/src/.ccache \
    build-windows-cross.sh

FROM windows-cross-build AS windows-cross-package
RUN package-windows-cross.sh

FROM debian:${DEBIAN_VERSION} AS windows-cross-artifacts
COPY --from=windows-cross-package /artifacts /artifacts
CMD ["cp", "-a", "/artifacts/.", "/out/"]

# ---------------------------------------------------------------------------
# Linux build.
# ---------------------------------------------------------------------------
FROM linux-base AS linux-build
ARG BUILD_TYPE=Release
ARG GIT_COMMIT=local
ARG GIT_REFSPEC=nx2
ARG BUILD_TAG=build-local
ARG BUILD_PLATFORM=linux
ARG ENABLE_WERROR=OFF
ARG ENABLE_OPENGL=ON
ARG ENABLE_LIBUSB=ON
ARG ENABLE_CUBEB=ON
ARG ENABLE_RESHADE=ON
ARG ENABLE_LSFG=ON
ARG ENABLE_UPDATE_CHECKER=OFF
ARG ENABLE_WEB_SERVICE=OFF
ARG ENABLE_QT_TRANSLATION=OFF

ARG BUILD_JOBS=24
ARG BUILD_NICE=0
ENV CMAKE_BUILD_PARALLEL_LEVEL=${BUILD_JOBS} BUILD_NICE=${BUILD_NICE}
ENV BUILD_TYPE=${BUILD_TYPE}
ENV GIT_COMMIT=${GIT_COMMIT}
ENV GIT_REFSPEC=${GIT_REFSPEC}
ENV BUILD_TAG=${BUILD_TAG}
ENV BUILD_PLATFORM=${BUILD_PLATFORM}
ENV ENABLE_WERROR=${ENABLE_WERROR}
ENV ENABLE_OPENGL=${ENABLE_OPENGL}
ENV ENABLE_LIBUSB=${ENABLE_LIBUSB}
ENV ENABLE_CUBEB=${ENABLE_CUBEB}
ENV ENABLE_RESHADE=${ENABLE_RESHADE}
ENV ENABLE_LSFG=${ENABLE_LSFG}
ENV ENABLE_UPDATE_CHECKER=${ENABLE_UPDATE_CHECKER}
ENV ENABLE_WEB_SERVICE=${ENABLE_WEB_SERVICE}
ENV ENABLE_QT_TRANSLATION=${ENABLE_QT_TRANSLATION}

WORKDIR /src
COPY . .
RUN printf '%s\n' "$GIT_REFSPEC" > GIT-REFSPEC \
    && printf '%s\n' "$GIT_COMMIT" > GIT-COMMIT
RUN --mount=type=cache,target=/src/.cache/cpm \
    --mount=type=cache,target=/src/.ccache \
    build-linux.sh

FROM linux-build AS linux-package
ARG BUILD_TAG=build-local
ARG BUILD_PLATFORM=linux
RUN BUILD_TAG="${BUILD_TAG}" BUILD_PLATFORM="${BUILD_PLATFORM}" package-linux.sh

FROM debian:${DEBIAN_VERSION} AS linux-artifacts
COPY --from=linux-package /artifacts /artifacts
CMD ["cp", "-a", "/artifacts/.", "/out/"]

# Unit tests: the Catch2 binary, run by the linux-tests service (TESTS filters the cases).
FROM linux-base AS linux-tests
ARG BUILD_TYPE=Release
ARG BUILD_JOBS=24
ARG BUILD_NICE=0
ENV CMAKE_BUILD_PARALLEL_LEVEL=${BUILD_JOBS} BUILD_NICE=${BUILD_NICE} BUILD_TYPE=${BUILD_TYPE} \
    LICHII_TESTS=ON LICHII_CMD=OFF
WORKDIR /src
COPY . .
RUN --mount=type=cache,target=/src/.cache/cpm \
    --mount=type=cache,target=/src/.ccache \
    build-linux.sh \
    && cp /src/build-tests/bin/tests /usr/local/bin/lichii-tests \
    && rm -rf /src/build-tests
CMD ["sh", "-c", "lichii-tests ${TESTS:-}"]

# The shared JIT code cache test under ThreadSanitizer, run by the linux-tsan service. Everything
# it links (common, dynarmic) is instrumented; no other target is built.
FROM linux-base AS linux-tsan
ARG BUILD_JOBS=24
ENV CMAKE_BUILD_PARALLEL_LEVEL=${BUILD_JOBS} BUILD_TYPE=RelWithDebInfo LICHII_TESTS=ON LICHII_CMD=OFF \
    BUILD_TARGETS=jit_shared_cache_tests \
    CFLAGS="-fsanitize=thread -fno-omit-frame-pointer" CXXFLAGS="-fsanitize=thread -fno-omit-frame-pointer" \
    LDFLAGS=-fsanitize=thread
WORKDIR /src
COPY . .
RUN --mount=type=cache,target=/src/.cache/cpm \
    --mount=type=cache,target=/src/.ccache \
    build-linux.sh \
    && cp /src/build-tests/bin/jit_shared_cache_tests /usr/local/bin/ \
    && rm -rf /src/build-tests
ENV TSAN_OPTIONS=suppressions=/src/src/tests/core/jit_shared_cache.tsan.supp
CMD ["jit_shared_cache_tests"]
