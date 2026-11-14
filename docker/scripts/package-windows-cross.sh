#!/bin/sh
# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu

QT_ROOT=/opt/Qt-win/6.8.3/msvc2022_64
out=/artifacts/windows-x86_64

export WINEPREFIX=/wine
export WINEDEBUG=-all
export WINEDLLOVERRIDES=mscoree,mshtml=
wineboot -u >/dev/null 2>&1 || true

wine "${QT_ROOT}/bin/windeployqt.exe" --release --no-translations \
    'Z:\src\build-windows\bin\lichii.exe'

mkdir -p "${out}"
cp -a build-windows/bin/. "${out}/"
cd "${out}"
zip -qr lichii-windows-x86_64.zip .
sha256sum lichii-windows-x86_64.zip > SHA256SUMS
