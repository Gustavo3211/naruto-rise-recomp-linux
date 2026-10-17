# Renderers and frame rate

[Documentation](README.md) · [Configuration](configuration.md#renderer) · [Build and run](build-and-run.md#native-renderer-optional)

The port can draw the game with two renderers, chosen at each launch. This page explains how they differ, how the native renderer presents 60 and 120 FPS, and what is still open. The option reference is in [Configuration](configuration.md#renderer).

## The two renderers

### Xenos (default)

The ReXGlue SDK's emulation of the Xbox 360 GPU, derived from Xenia. It executes the command buffers the game writes for the GPU, translates the game's shaders the first time they appear, and keeps them in the user cache; the bundled shader storage fills that cache in advance. It is the most tested path, supports 21:9 and 32:9, and runs the game at its original frame rates.

### Native (experimental)

The native renderer replaces the GPU emulation. It intercepts the game's Direct3D calls (device, resources, render states, draws, and presents) and issues them with Direct3D 12 through [Plume](https://github.com/renderbag/plume). The game's shaders are translated ahead of time by the XenosRecomp fork in `tools/xenosrecomp/`, compiled when the port is built, and embedded in `narutorise.exe`, so no shader compiler runs during play. Builds that carry a list of the pipelines seen in play compile it at startup; a pipeline outside that list compiles in the background the first time it appears.

Without the emulation layer, it offers options that Xenos does not have: MSAA, transparency antialiasing, a 1.5× internal resolution, brightness, frame latency, triple buffering, and frame rates above 30.

It has been played through the menus, the open world, missions, and fights, and compared with Xenos, but it is not verified across the whole game. Xenos remains the default.

### Comparison

| | Xenos | Native |
| --- | --- | --- |
| Drawing | Emulated Xbox 360 GPU (Direct3D 12 backend) | The game's Direct3D calls on Direct3D 12 |
| Shaders | Translated on first appearance, cached per user | Translated at build time, embedded in the executable |
| Frame rate | The game's: 30, and 60 in fights | 30, 60, or 120 in the launcher; up to 240 in `narutorise.toml` |
| Aspect ratio | 16:9, 21:9, 32:9 | 16:9, with bars on wider monitors |
| Resolution | 1× to 4× | 1×, 1.5×, 2× to 4× |
| Anti-aliasing | FXAA, CAS/FSR | The game's 4× MSAA or 2×/8×, transparency antialiasing, FXAA, CAS/FSR |
| Frame rate display | `F1` overlay | Window title |

## Choosing the renderer

Open **Settings → Display → Renderer** in the launcher, or set `renderer` in `narutorise.toml`:

```toml
renderer = "native"   # or "xenos"
```

The choice takes effect on the next launch. The launcher shows the options of both renderers: those the selected renderer does not use are greyed out and tagged, and the description panel says why. Builds without the native renderer ignore the key and leave its options out of the launcher; see [Build and run](build-and-run.md#native-renderer-optional) to include it.

Both renderers honor the graphics card selected in the launcher. The log names the card in use: `DXGI adapter:` for Xenos, `Native renderer adapter:` for the native renderer.

## Frame rate

### The game's rates

The game advances its logic in fixed steps: 1/30 s in the open world and menus, and 1/60 s in fights. Animation, physics, and timers depend on that step, so the console's frame rate follows it. The Xenos renderer keeps these rates.

### Frames between the logic steps

With the native renderer, the **Frame rate** slider in **Settings → Display** offers 30, 60, and 120 (`frame_rate`, 60 in the shipped configuration). At 30, the game runs as on the console. Above 30, the game's logic keeps its own step, and the renderer draws the frames in between. For each of those frames it:

- places objects and the camera between their positions at the last two logic steps;
- does the same for the characters' skinned poses;
- advances visual effects, such as particles and dust, by the frame's share of the step.

Game speed, physics, and timing stay those of the console, because the logic itself does not change.

`narutorise.toml` accepts any rate from 30 to 240. Where the step is not well below the frame time, the frames follow the step instead, so the game keeps its speed: fights show at 60 when `frame_rate` is below `76`, and the open world shows at 30 when it is between `31` and `37`. With VSync on, the rate does not exceed the monitor's refresh rate; choose 60 for a 60 Hz monitor and 120 for a 120 Hz or faster one.

### Limits

- The HUD minimap still updates at 30 FPS; the rest of the image is interpolated.
- The image trails the logic by one step: up to about 33 ms in the open world and 17 ms in fights.
- Busy areas of the open world may not reach 120 FPS. Choose 60 if the rate varies.

### Measuring the frame rate

- **Xenos:** `F1` shows the **guest** rate, the game's own frame production that game speed follows, and the **host** rate, the window present rate.
- **Native:** the window title shows the rate and the frame time of the last half second, for example `| 60.0 fps | 16.7 ms (max 18.1)`. It is visible in windowed mode. Set the environment variable `REX_NATIVE_FPS_TITLE=0` to hide it.

## Known issues

These apply to the native renderer. Report anything else through the [issue tracker](https://github.com/jorjbarbosa/naruto-rise-recomp/issues), including the renderer and frame rate you use.

| Issue | Details |
| --- | --- |
| Slow close | With a controller connected, closing the window can leave a game thread waiting; the game gives its threads 3 seconds and then exits. Closing right after a fight has also been slow. |
| Objects missing for a moment | A pipeline outside the startup list compiles the first time it appears, and its draws are skipped until it is ready. This was seen with new matchups in Fight Mode. |
| Light glows | The game uses occlusion queries only for light glows, and the native renderer answers them with a fixed value: a glow behind a wall stays visible, and large or close glows look dimmer than with Xenos. |
| Minimap at 30 FPS | See [Limits](#limits). |
| No ultrawide | The image stays 16:9, with bars on wider monitors. |
| No `F1`/`F3` overlays | Use the window title to read the frame rate. |
| Crash in a long session | A crash after about 16 minutes of play has been reported once with an earlier test build; the cause is under investigation. |

## Reporting problems

Include the renderer, the frame rate, VSync, MSAA, internal resolution, the graphics card and driver, and the log from `logs/` next to the executable. For speed or smoothness problems, include the frame rate shown by `F1` (Xenos) or in the window title (native). If a problem appears only with the native renderer, check whether it also happens with `frame_rate = 30`.
