<!--
# SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

# SPDX-License-Identifier: GPL-2.0-or-later
-->

# Lichii (tasarren fork)

A fork of [zortos293/Lichii](https://github.com/zortos293/Lichii), a Nintendo Switch and Switch 2
emulator. This fork works on speed, memory use and stutter, tested mostly with The Legend of Zelda:
Ocarina of Time. It follows Lichii's `nx2` branch; our changes are on the `next` branch.

Downloads: [releases](https://github.com/tasarren/Lichii/releases). Games, keys and firmware are not
included; use only what you legally own.

## Settings for the best speed (Ocarina of Time)

Open **Emulation > Configure** (or press Ctrl+,), then **Graphics**. Each step names the tab.

**Graphics card with 4 GB (for example GTX 1050 Ti, GTX 1650, RX 570 4 GB)**

- Graphics: Resolution 1X.
- Graphics: Ocarina of Time render scale: Ultra Performance (1280x720).
- Graphics: Frame rate limit (Switch 2): 60.
- Advanced: GPU Mode: Low.
- Advanced: VRAM Usage: 2 GB.
- Advanced: RAM Usage Mode: Low.
- Advanced: ASTC Recompression Method: BC1.
- Extras: Enable asynchronous shader compilation: on.

Not yet tested on a real 4 GB card. On our test PC the game needed about 4 GB of video memory at
1920x1080 even with the 2 GB setting, so a 4 GB card needs the smaller 1280x720 render scale. Close
other programs that use the graphics card (browsers, video players) while you play.

**16 GB of memory, or a graphics card with 6 to 8 GB**

- Graphics: Resolution 1X.
- Graphics: Ocarina of Time render scale: Performance.
- Graphics: Frame rate limit (Switch 2): 60.
- Advanced: GPU Mode: Low.
- Advanced: VRAM Usage: 4 GB.
- Advanced: RAM Usage Mode: Low.
- Advanced: ASTC Recompression Method: BC3.
- Extras: Enable asynchronous shader compilation: on.

**In between**

- Graphics: Resolution 1X.
- Graphics: Ocarina of Time render scale: Balanced.
- Graphics: Frame rate limit (Switch 2): 60.
- Advanced: GPU Mode: Low.
- Advanced: VRAM Usage: Auto.
- Advanced: RAM Usage Mode: Auto.
- Advanced: Precache shaders before game start: Always.
- Extras: Enable asynchronous shader compilation: on.

**32 GB of memory and a graphics card with 12 GB or more**

- Graphics: Resolution 1X.
- Graphics: Ocarina of Time render scale: Native.
- Graphics: Frame rate limit (Switch 2): 120.
- Graphics: VSync Mode: FIFO (VSync).
- Advanced: GPU Mode: Low.
- Advanced: VRAM Usage: Auto.
- Advanced: RAM Usage Mode: High.
- Advanced: Build all cached shaders before game start: on.

Above 60 the game plays too fast unless FPS Unlock is on. It is on by default. To check: in the game
list, right-click Ocarina of Time, choose Properties, open the Add-Ons tab, and leave "FPS Unlock
(built-in)" ticked.

### What each choice changes

- **Resolution** is the emulator's own scaling. 1X uses the least video memory. Higher uses more.
- **Ocarina of Time render scale** is the size the game itself draws at. Native is sharpest.
  Performance is 1920x1080 and the smoothest.
- **Frame rate limit (Switch 2)** is how often the game draws. 60 matches the original. Higher is
  smoother, but only with FPS Unlock, or the game speeds up.
- **VSync Mode** stops the picture tearing. FIFO (VSync) never tears. Mailbox can feel slightly
  quicker but sometimes drops a frame.
- **GPU Mode** Low runs faster. High is more accurate and slower; this game does not need it.
- **VRAM Usage** is how much video memory the emulator keeps. Auto uses most of your card and leaves
  some for everything else. 4 GB keeps the game inside a small card.
- **RAM Usage Mode** is how many shaders stay loaded. High means no pop-in and more memory. Low uses
  less memory; a place you revisit can flicker for a moment while its shaders reload.
- **ASTC Recompression Method** BC3 stores textures in less video memory, and the picture loses a
  little quality. BC1 saves more and loses more; use it only on a 4 GB card.
- **Precache shaders before game start** builds the game's shaders while the loading screen is up,
  so the first time you do something new does not hitch. The first launch takes a few minutes.
- **Build all cached shaders before game start** does the same for everything from your earlier
  sessions. Loading takes longer and uses more memory.
- **Enable asynchronous shader compilation** lets the game keep running while a new shader builds,
  instead of freezing.

### If the game stutters

- Turn on Precache shaders before game start, and give it a few minutes the first time.
- On a PC with memory to spare, turn on Build all cached shaders before game start.
- The first time you visit a place or use an item, expect a short pause. It happens once.

### If the game runs slow

- Set Ocarina of Time render scale to Performance.
- Set RAM Usage Mode to Low and VRAM Usage to 4 GB (2 GB on a 4 GB card).
- Set the frame rate limit to 60.
- Set GPU Mode to Low.

### If the game crashes

- Set VRAM Usage to Auto.
- Update your graphics driver.
- If it still crashes, use Help > Report a Bug and attach the file it makes to an
  [issue](https://github.com/tasarren/Lichii/issues).

## Copy-paste configuration

The same settings as config file lines, if you prefer to edit the file. Close Lichii first. All keys
go in the `[Renderer]` section of `qt-config.ini`:

- Windows: `%APPDATA%\lichii\config\qt-config.ini`
- Linux: `~/.config/lichii/qt-config.ini`

If a key is already there, change its lines instead of adding a second copy. Each setting needs its
`\default=false` line, or Lichii ignores the value.

**4 GB graphics card**

```ini
[Renderer]
resolution_setup\default=false
resolution_setup=3
ocarina_render_scale\default=false
ocarina_render_scale=3
nx2_frame_rate_limit\default=false
nx2_frame_rate_limit=60
gpu_accuracy\default=false
gpu_accuracy=0
vram_usage_mode\default=false
vram_usage_mode=2
ram_usage_mode\default=false
ram_usage_mode=3
astc_recompression\default=false
astc_recompression=1
use_asynchronous_shaders\default=false
use_asynchronous_shaders=true
```

**Low (16 GB memory or 6 to 8 GB graphics card)**

```ini
[Renderer]
resolution_setup\default=false
resolution_setup=3
ocarina_render_scale\default=false
ocarina_render_scale=2
nx2_frame_rate_limit\default=false
nx2_frame_rate_limit=60
gpu_accuracy\default=false
gpu_accuracy=0
vram_usage_mode\default=false
vram_usage_mode=3
ram_usage_mode\default=false
ram_usage_mode=3
astc_recompression\default=false
astc_recompression=2
use_asynchronous_shaders\default=false
use_asynchronous_shaders=true
```

**Mid**

```ini
[Renderer]
resolution_setup\default=false
resolution_setup=3
ocarina_render_scale\default=false
ocarina_render_scale=1
nx2_frame_rate_limit\default=false
nx2_frame_rate_limit=60
gpu_accuracy\default=false
gpu_accuracy=0
vram_usage_mode\default=false
vram_usage_mode=0
ram_usage_mode\default=false
ram_usage_mode=0
precache_shaders\default=false
precache_shaders=2
use_asynchronous_shaders\default=false
use_asynchronous_shaders=true
```

**High (32 GB memory and 12 GB+ graphics card)**

```ini
[Renderer]
resolution_setup\default=false
resolution_setup=3
ocarina_render_scale\default=false
ocarina_render_scale=0
nx2_frame_rate_limit\default=false
nx2_frame_rate_limit=120
use_vsync\default=false
use_vsync=2
gpu_accuracy\default=false
gpu_accuracy=0
vram_usage_mode\default=false
vram_usage_mode=0
ram_usage_mode\default=false
ram_usage_mode=1
build_all_cached_shaders\default=false
build_all_cached_shaders=true
```

Values: `resolution_setup` 3 = 1X. `ocarina_render_scale` 0 Native, 1 Balanced, 2 Performance, 3 Ultra Performance.
`use_vsync` 2 = FIFO. `gpu_accuracy` 0 Low, 1 High. `vram_usage_mode` 0 Auto, 1 Unlimited, 2 = 2 GB,
3 = 4 GB, 4 = 8 GB. `ram_usage_mode` 0 Auto, 1 High, 2 Balanced, 3 Low. `astc_recompression` 1 = BC1, 2 = BC3.
`precache_shaders` 2 = Always.

Settings for one game only live in `custom/<title id>.ini` next to `qt-config.ini` (Ocarina of
Time: `custom/0400486024DEC000.ini`). There a key also needs `\use_global=false`; the game's
Properties window writes these for you.

## Reporting a problem

Open an [issue](https://github.com/tasarren/Lichii/issues) with the build number, the game, your
CPU, GPU and driver, your settings, and the log (File > Open Lichii Folders > Log Folder) or a Help > Report a Bug
file. Check it for personal information first. Do not upload games, keys or firmware.

## Building

See the [build guide](docs/Build.md).

## Credits and license

This fork builds on Lichii and the projects it comes from: Eglantine, Eden, yuzu and Sudachi, and
their contributors. Original copyright notices and third-party licenses are kept. See
[LICENSE.txt](LICENSE.txt) and the source files for the license terms. Not affiliated with Nintendo.
