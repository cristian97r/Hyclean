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

In the game list, right-click Ocarina of Time and open **Properties**. The **Game** tab has the
game's render scale and anti-aliasing controls. Use **Graphics** in Properties for per-game
graphics settings, or **Emulation > Configure > Graphics** to change the defaults.

### Start with render scale

On a slower graphics card, lower **Ocarina of Time render scale** first. It changes how large the
game draws the scene when docked with the **Max Resolution** add-on enabled. Lower scales give a
softer picture and can raise frame rate when the GPU, not the CPU, is the limit. In one Market
test at **1X**, **Ultra Performance** used about 250 MiB less video memory than **Native**.
Restart the game after changing this setting.

| Render scale | Size the game draws |
|---|---|
| Native (maximum) | 2560×1440 |
| Balanced | 2276×1280 |
| Performance | 1920×1080 |
| Ultra Performance | 1280×720 |

Start with **Resolution** at **1X**, **GPU Mode** at **Fast**, and the Switch 2 frame rate limit
at **60**. **GPU Mode: Accurate** is slower, and Ocarina of Time does not need it. Try the
**Performance** render scale if your GPU struggles, then **Ultra Performance** if needed.
For more than 60 fps, leave **FPS Unlock (built-in)** on under **Properties > Add-Ons** so the
game does not speed up.

### Pick an upscaler

In **Graphics**, **Upscaler** offers **Native** (the game's own upscaler), **AMD FSR**, and
**NVIDIA DLSS**. In Ocarina of Time, FSR and DLSS only apply at **Resolution: 1X**. This is the
emulator's Resolution setting, not the game's render scale. DLSS can set its own render scale
based on the selected DLSS quality mode.

On Linux, DLSS needs a GeForce RTX card, a driver with NVIDIA NGX support, and the separate
[Lichii DLSS module](https://github.com/tasarren/lichii-dlss/releases). Select **NVIDIA DLSS** in
**Graphics**, then use **Download**. Lichii shows NVIDIA's license before fetching the module
and NVIDIA's DLSS files. If you already have the files, use **Install from file...** and select
`lichii-dlss-module.so` with NVIDIA's DLSS model in the same folder. Once the panel says **Ready**,
start the game again.

### Reduce first-visit freezes

Keep **Enable asynchronous shader compilation** on. **Skip mesh draws that are still building**
is on by default. It reduces pauses when you first visit an area or see a new effect. Some objects
may briefly pop into view when their shaders finish building. If you prefer to wait, turn it off
in **Graphics > Advanced** or for this game in **Properties > Adv. Graphics**.

Keep **Build all cached shaders before game start** on for fewer pauses in places you have
already visited, at the cost of a longer load. New scenes can still pause while shaders build.
Leave **Precache shaders before game start** off. It guesses shader inputs and misses the slow
shaders that cause these pauses.

### Tune memory use

To save video memory, start with **Properties > Game > Ocarina of Time local shadow size**.
It controls shadows for nearby objects in towns. Compared with **Full**, **Half** saves about
216 MiB for the shadow map and **Quarter** saves about 270 MiB. Shadows looked nearly the same
in Market tests. Restart the game to apply the change.

| Setup | Render scale to try | Shader Pipeline Memory | Texture Memory Target |
|---|---|---|---|
| Limited system RAM and a slower GPU | Performance | Low | Auto |
| Mid-range setup | Balanced | Auto | Auto |
| 32 GB RAM and 12 GB or more on the graphics card | Native | High | Auto |

These are starting points, not hardware requirements or guarantees. Lichii converts the game's
ASTC textures for desktop graphics cards. Under **ASTC Recompression Method**, **BC3** saves video
memory at a small cost to texture quality. **Uncompressed** uses the most video memory.

- **Shader Pipeline Memory** (in **System**) limits the shader pipelines and shader data Lichii
  keeps, not how much RAM the game uses. In warm Market runs at 1X and 720p render scale, **Low**
  used about 900 MiB less RAM than **Auto**. Low can mean more shader rebuilds on return visits.
- **Texture Memory Target** (in **Graphics > Advanced**) applies only to cached textures. Render
  targets, buffers, and the driver still need memory. In Market tests at **1X**, total video memory
  use was about 4.5–5.2 GiB, with about 4.5 GiB even at the 2 GB and 4 GB targets. Neither a low
  target nor a lower render scale guarantees the game will fit on a 4 GB card. If a low target
  makes play less smooth, try **Auto**.

If the game crashes, try **Texture Memory Target: Auto** and update your graphics driver. If it still
crashes, see [Reporting a problem](#reporting-a-problem).

## Copy-paste configuration

Example presets if you prefer to edit the config file. Close Lichii first. All keys go in the
`[Renderer]` section of `qt-config.ini`:

- Windows: `%APPDATA%\lichii\config\qt-config.ini`
- Linux: `~/.config/lichii/qt-config.ini`

If a key is already there, change its lines instead of adding a second copy. Each setting needs its
`\default=false` line, or Lichii ignores the value.

**Low (limited system RAM and a slower GPU)**

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
vram_usage_mode=0
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
build_all_cached_shaders\default=false
build_all_cached_shaders=true
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
`use_vsync` 2 = FIFO. `gpu_accuracy` 0 Fast, 1 Accurate. `vram_usage_mode` 0 Auto, 1 Unlimited, 2 = 2 GB,
3 = 4 GB, 4 = 8 GB. `ram_usage_mode` 0 Auto, 1 High, 2 Balanced, 3 Low. `astc_recompression` 2 = BC3.

Settings for one game only live in `custom/<title id>.ini` next to `qt-config.ini` (Ocarina of
Time: `custom/0400486024DEC000.ini`). There a key also needs `\use_global=false`; the game's
Properties window writes these for you.

## Reporting a problem

Open an [issue](https://github.com/tasarren/Lichii/issues) with the build number, the game, your
CPU, GPU and driver, your settings, and the log (File > Open Lichii Folders > Log Folder) or a Help > Report a Bug
file. Check it for personal information first. Do not upload games, keys or firmware. Bug
bundles, GPU traces, shader dumps, pipeline caches and crash dumps can contain game data: do not
attach them to public reports; ask a maintainer first.

## Building

See the [build guide](docs/Build.md).

## Credits and license

This fork builds on Lichii and the projects it comes from: Eglantine, Eden, yuzu and Sudachi, and
their contributors, including Eglantine's original developer Camille LaVey. Original copyright
notices and third-party licenses are kept. See [LICENSE.txt](LICENSE.txt) and the source files for
the license terms. Not affiliated with Nintendo.
