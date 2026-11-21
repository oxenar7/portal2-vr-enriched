# Portal 2 VR Enriched

An incremental modernization fork of [Gistix/portal2vr](https://github.com/Gistix/portal2vr), built on [mmadej20/portal2vr-ultimate](https://github.com/mmadej20/portal2vr-ultimate), for the original Windows version of Portal 2 and for Portal: Alive & Kicking, a free fan-made campaign on the same engine. This remains a 32-bit Source/DX9 runtime-hooking mod, not a replacement engine or a rewrite. SteamVR / OpenVR and the modified DXVK rendering path are retained.

The name nods to the Aperture Science Enrichment Center and to what this fork adds to the VR experience.

This is an unofficial fan project, not affiliated with or endorsed by Valve. You need your own copy of the game; no game files are included.

**Tested setup:** the whole Portal 2 campaign has been played on one setup (Quest 3 through Virtual Desktop, RTX 4070), and Alive & Kicking has been tested on the same setup. Co-op reports from players: hours of co-op running well on two Steam Frame headsets, and a full co-op run with both players in VR and no problems (the final gesture needs a keyboard key, see [If something goes wrong](#if-something-goes-wrong)). Other headsets and machines are untested, so logs from them are welcome. Hooks are tied to the current Steam build of Portal 2, so a game update can disable them until the mod is updated.

## What's new in this fork

- **Portal: Alive & Kicking in VR:** the fan-made campaign runs with the same features as Portal 2, including its own menus, chapters and portal gun. Pick it on the launcher's **Games** page.
- **The gun fits your hand in every game:** the mod reads which portal gun model the game loaded (Portal 2's, Alive & Kicking's or the HD Collection's VR gun) and matches its grip by itself.
- **Physical crouch:** duck in real life and you duck in the game. The camera lands on Source's ducked eye height and eases back as you stand.
- **Held objects stay on the gun:** they move and turn with your wrist, walls still stop them, and lasers leaving a reflector cube steer onto catchers as they do in co-op.
- **More haptics:** Portal 2's own rumble, plus pickup, drop, holding and button pulses.
- **VR options inside Portal 2's menus:** Options > VR, drawn in the game's own style and usable with the laser or the stick.
- **DXVK 2.6.8 (DXVK-GPLALL):** asynchronous pipelines and low-latency pacing, stereo frames no longer wait for the GPU to go idle.
- **Steadier portal gun:** it pivots at its body, so it no longer shakes with your head.
- **Readable captions:** an opaque backing, placed just below your gaze and following it.
- **Button hints you can see:** Portal 2's on-screen hints now appear in the headset and show the controller button bound to that action in SteamVR, not a keyboard key.
- **Binocular zoom:** hold the right stick in to look through a magnified binocular window. It also completes the game's zoom lesson.
- **Anti-aliasing at full frame rate:** with anti-aliasing on, stereo frames no longer wait for the GPU, and the parts of each eye image the headset never shows are not drawn. On the tested setup, 4x anti-aliasing went from 46 to a steady 90 fps.
- **Smooth grates and sharp floors:** with anti-aliasing on, grate holes and railing cut-outs get smooth edges instead of crawling stair steps; 16x anisotropic filtering keeps floors and walls sharp at grazing angles.
- **Aim through grates:** the aim line and reticle pass through grates and railings, like the portal shot itself.
- **Use what the gun points at:** pick up, drop and press act on what the portal gun points at, not on what you look at. The aim line turns orange, and the gun hand gives a light pulse, when something usable is under it.
- **Pause menu in the world:** pausing opens panels in Aperture style, floating in front of you over the paused level: resume, save, load, VR options and exit, picked with the controller laser and Portal 2's own menu sounds. Portal 2's own pause screen is one click away, or the default with **Pause menu in the world** off in the launcher.
- **Steady funnels with roomscale:** walking in your room no longer fights the pull of an excursion funnel, so the view stays still while you float.
- **Left-handed play:** left-handed mode moves the gun's buttons with it, and a separate option swaps the sticks.
- **A small, clean launcher** with a **Games** page: it finds Portal 2, Alive & Kicking and Steam by itself, saves settings as you change them, keeps the everyday options up front and the calibration under **Advanced settings**. It only needs the Python standard library when run from source.

## Install

You need:

- Portal 2 or Portal: Alive & Kicking on Steam, for Windows.
- SteamVR working with your headset. Quest users can connect through Virtual Desktop, Steam Link or Quest Link.
- The latest [Microsoft Visual C++ Redistributable for x86](https://aka.ms/vc14/vc_redist.x86.exe). Install the x86 one even on 64-bit Windows.
- A graphics driver with Vulkan support (any recent NVIDIA, AMD or Intel driver).

Then:

1. Download `Portal2VR-Enriched-vX.Y.Z-win32.zip` from the [Releases](https://github.com/oxenar7/portal2-vr-enriched/releases) page and extract the whole ZIP to a folder you keep, for example in Documents. Python and build tools are not needed.
2. Close Portal 2, connect your headset and open `Portal 2 VR Enriched Launcher.exe`. The launcher is not code-signed, so Windows may show "Windows protected your PC": choose **More info**, then **Run anyway**.
3. The launcher opens on its **Games** page and finds your games and Steam by itself, including Steam libraries on other drives. Click the game you want to play. If a game shows as not found, press **Locate folder** and pick the folder that contains `portal2.exe` (in Steam: right-click the game > Manage > Browse local files); for Steam, press **Change** on the Steam card and pick `steam.exe` in your Steam folder.
4. Press **Play in VR**. The launcher backs up the game files it replaces, installs the mod and starts Portal 2 through Steam with the launch options it needs. SteamVR starts with the game. Next time, just press **Play in VR** again.
5. Optional: turn on subtitles in Portal 2's audio options to get captions in the headset.

The mod stays installed after you quit. To play the game normally again, open the launcher, select it and press **Restore original files** on the Games page. Each game keeps its own backups. Keep the extracted folder until then: it holds your backups.

To update, close the launcher and extract the new ZIP into the same folder, replacing the files. Your settings and backups stay, and the next **Play in VR** installs the new version. If you prefer a new folder, first press **Restore original files** in the old launcher.

## Controls

Default bindings for Quest / Oculus Touch controllers:

| Action | Button |
| --- | --- |
| Move | Left stick |
| Turn (smooth or snap) | Right stick |
| Blue portal | Right trigger |
| Orange portal | Right grip |
| Use, pick up, drop | B, on what the gun points at |
| Jump | A |
| Crouch | Left grip, or crouch for real |
| Binocular zoom | Hold the right stick in |
| Pause menu | Y |
| Menus | Point and pull the trigger; Portal 2's own menus also take the left stick with A and B |

**Left-handed mode** puts the gun in the left hand, together with its buttons: blue portal on the left trigger, orange portal on the left grip, use on Y. Crouch moves to the right grip and the pause menu to B. **Swap sticks** is a separate option: move with the right stick, turn with the left, and hold the left stick in for binocular zoom. Both are in Options > VR and in the launcher.

Valve Index and Vive Cosmos controllers have their own default bindings. Other controllers can be bound in SteamVR: Settings > Controllers > Manage Controller Bindings. **Recenter** is in Options > VR > Advanced VR. Turning, movement direction, left-handed mode and vibration are in Options > VR.

## If something goes wrong

- **The game does not start in VR:** make sure SteamVR sees the headset and controllers before pressing **Play in VR**.
- **The game closes at startup:** install the latest Visual C++ Redistributable for x86 from the link above.
- **The end of the co-op campaign asks for a gesture:** the game's gesture key has no controller button yet. Press **Q** on the PC keyboard when the game asks for it. A fix is planned.
- **After a Portal 2 update:** some features may switch themselves off until the mod is updated. The log says which.
- **The portal gun sits away from your hand:** the mod reads which portal gun model the game loaded and matches its grip: Portal 2's own, Alive & Kicking's, and the "Portal VR Gun Model" replacement included in the Portal 2 VR HD Collection, which holds the gun differently. If you set Gun offset X / Y / Z by hand for that pack (`-7.3` / `-8.2` / `11.35`), the launcher puts them back to `0` by itself. Another replacement model may need its own offsets: in the launcher's **Settings**, turn on **Advanced settings** and adjust them under **Gun position**; `bin/portal2vr.log` names the model it found ("Portal gun model: …").
- **Dark or black wedges at the edge of your view, or a black world where only the portals show:** the mod skips the eye pixels your headset reports as never shown. Since v1.1.2 it ignores a report that would hide the middle of the view or most of it (the log line `Eye mask ...` then ends with "lens mesh ignored"). If you still see black areas, create a text file named `dxvk.conf` next to `portal2.exe` in your game folder containing `d3d9.vrEyeMask = False`, and please report your headset and how it connects to the PC.
- **Reporting a problem:** press **Open mod log** in the launcher, or find `bin/portal2vr.log` in your Portal 2 folder, and attach it to a GitHub issue with your headset and graphics card.

## Settings

The launcher starts with the **recommended setup**. Settings save as you change them and only touch the launcher's own preferences; **Restore recommended** puts them back. **Play in VR** installs them with the mod, and **Apply settings** on the Games page does the same without starting the game. Apply with the game closed; restart to use the new settings. The basic view shows the everyday options; **Advanced settings** adds calibration, tracking, caption placement and diagnostics.

### Recommended setup

The launcher default includes features confirmed during the maintainer's headset sessions:

- Seated tracking with positional head movement; Standing is available in Settings.
- Physical roomscale walking with player/body feedback, rather than moving only the camera away from the collision body. `RoomscaleMode=ActiveExperimental` uses guarded `CUserCmd` movement; `Off` and read-only `Observe` remain available.
- LegacyYaw portal traversal, HMD-relative stick movement and smooth turning. Snap turning is selectable.
- Controller-locked portal-gun model and glow, with current calibration offsets. The **aim line** starts at the animated muzzle.
- The **original Portal reticle and dynamic blue/orange status** at the aim endpoint, not custom replacement artwork.
- Readable subtitle overlay and pause/menu placement corrected after physical walking.
- Portal-shot haptics, with bounded amplitude/duration and duplicate-event suppression.
- Quiet diagnostics: startup, changed render geometry and failures are logged. Repeated allocation/MSAA details and periodic summaries are off; enable **Verbose diagnostics** in Advanced when investigating them. The extra desktop render is off.

These observations concern the tested installation, not every headset, game build or portal orientation. The versioned `L4D2VR/config.txt` retains conservative default-off experimental flags; the launcher supplies the recommended profile.

### Configuration and recovery

Settings provides tracking, movement, roomscale, turning and desktop-render options. Advanced exposes all current mod parameters with descriptions/bounds: caption placement, binocular zoom, gun calibration, IPD/world scale, controller pitch, haptics, portal orientation, anti-aliasing, aiming and diagnostics. Invalid numbers and incompatible roomscale/muzzle settings are rejected before deployment. Preview config, on the Advanced page, shows what will be written.

### In-game VR options

Portal 2's own **Options** screen gains a **VR** entry under Controller. It opens a **VR** dialog styled like Video, drawn with the game's tiles, fonts and menu sounds: snap/smooth turning, movement direction, left-handed mode, vibration and the aim line, plus an **Advanced VR** page (positional tracking, controller pitch, standing height, world and IPD scale, individual vibration sources, reticle scaling, desktop mirror). Point and click with the laser, or use the stick: up/down to select, left/right to change, Back to leave. Changes apply immediately; world scale and standing height apply on **Recenter**. Settings that need a restart stay in the launcher.

Changes are saved to `VR/ingame_settings.txt`, which overrides matching keys of `VR/config.txt`; **Use Defaults** deletes it. The launcher takes those values on its next start, once (a later change in the launcher wins over the same file), and folds them into `config.txt` on Apply settings/Play in VR, then removes the file. The page draws only after every VGUI vtable slot it uses matches a prologue verified on the installed `vguimatsurface.dll`/`vgui2.dll`; otherwise it is skipped with a `bin/portal2vr.log` warning and `config.txt` keeps working.

`Restore recommended` resets the form without changing installation paths or silently applying anything. Turning off **Aim line** leaves only the Portal reticle at the aim point.

Before first installation the shared installer takes a hash-verified snapshot of any existing files it will replace. Later updates retain that first snapshot. **Restore original files** restores pre-existing files and removes only files created by the launcher; backups remain available. Externally edited managed files, corrupted backups and concurrent installer operations are refused rather than overwritten.

Keep these local files together when moving the checkout or recovering an installation:

- `tools/.launcher-state.json`: shared install ownership and developer checklist.
- `tools/.launcher-backups/`: original snapshots and manifests.
- `tools/.user-launcher-settings.json`: user settings, separate from install ownership.

Do not delete install state/backups to fix an error. If only user preferences are malformed, copy that preferences file aside before replacing it; startup does not silently reset it. Open mod log accesses `bin/portal2vr.log` in the selected game folder. The older `python -m tools.test_launcher` remains available for manual tests using the same backups/transaction lock. Changing settings in one launcher does not silently synchronize the other launcher's form.

## Changes from the original mod

- Digital inputs send press/release commands on state transitions instead of repeating release commands every frame. One-shot actions trigger only on press transitions.
- HMD/controller indices and pose validity are checked; disconnected/untracked controllers do not become unchecked pose-array accesses or current-looking stale hand poses.
- OpenVR subsystem/action startup errors and required/optional hook resolution have explicit failure handling and persistent Release diagnostics. Missing `-insecure` is explained instead of silently terminating the game.
- Config parsing has bounds and valid-value fallback. Reloading uses a managed watcher lifecycle and logs errors instead of repeated background dialogs.
- Shared tracking-space conversion, independent hand offsets, recenter/turn coherence, Standing height handling and selectable locomotion direction were added.
- Experimental roomscale feedback includes replay guards, manual-input priority and portal/reset handling. Experimental 3D portal orientation modes are separate from recommended LegacyYaw.
- VR startup/render-target readiness and menu placement were stabilized across the mod and modified DXVK.
- Stereo aiming/HUD projection and native reticle clipping were corrected. Gun/effect alignment and animated muzzle sampling place the aim line. Portal 2's own particle beam never showed in the headset, so its code path was removed.
- Subtitle capture/placement, roomscale pause-menu anchoring and menu click releases were improved; local portal-shot haptics were added.
- DXVK and MinHook are ordinary pinned source files instead of submodules, retaining licenses/provenance in `THIRD_PARTY_SOURCES.txt`. DXVK is DXVK-GPLALL 2.6.8-2 carrying the VR hooks, not generic latest DXVK.
- A user-facing launcher provides validated advanced settings, original backups, transactional staging/restore, config preview and diagnostic shortcuts. Pure-component C++ and offline Python installer/GUI tests cover reproducible behavior without the Source runtime.

## Known limitations

- Portal 2's own `robot_point_beam` particle laser is not used: it never showed in the headset. The aim line and the original Portal reticle take its place.
- FullRotation, YawOnly and PreserveHorizon are manual experiments. FullRotation has limited wall-portal evidence; floor/ceiling comfort, recenter edge cases and broader traversal need testing. Active roomscale requires LegacyYaw and 6DOF.
- Reticle distance scaling is modest (100% at 1 m to 80% at 10 m); a dedicated far-distance readability comparison remains outstanding.
- Signatures, offsets, vtables and the 32-bit engine ABI remain compatibility risks. No universal compatibility, performance or 120 Hz frame-rate claim is made. Co-op has not been tested by the developer. Two player reports say it works well, one of them a full run with both players in VR; the final gesture still needs **Q** on the keyboard.
- `EyePosition`, `Weapon_ShootPosition` and `GetViewModelFOV` use audited RVAs only after the DLL's SHA-256 and its loaded code/table references match the supported build. The shoot-origin override applies only to the verified `FirePortal` call, preserving other calls to the shared function. An unsupported DLL, failed verification or failed hook installation produces a warning in `bin/portal2vr.log` and skips that hook without blocking startup; Source's original shoot position or viewmodel FOV may be used. The user reported successful hardware testing on one current setup; other game builds and setups require separate validation.
- No OpenXR backend, optimized desktop mirror, Portal Reloaded ThirdAttack support or complete HUD rewrite.
- Controller haptics (`Haptics=true` by default) forward Portal 2's own rumble user message (portal shots, failed placement, fizzler, landings, physics impacts) through optional client/server hooks, and add pickup/drop/hold and pedestal/floor-button pulses. Each hook that fails to resolve is logged in `bin/portal2vr.log` and only disables its own vibration source. Every native effect is logged as `Native rumble: ...`. Pulse strengths are tuned for one Quest/Virtual Desktop setup.

## Selective PCVR improvements

The `native-vr` fork was reviewed at commit [`0094269`](https://github.com/iFeelLikeChicken2Nite/portal2vr-ultimate/commit/0094269795aa29d2b0477af00e082b8b686aa68c). The changes here adapt its menu-refresh and HUD ideas independently of Sixense. No fork commits were cherry-picked, and no Sixense/Hydra proxy, bindings, module aliases, calibration or alternate engine ABI were added.

Render targets retain their existing ownership and bounded retry policy. A return from gameplay to the menu/loading state requests one compatibility refresh instead of recreating every menu frame. A real D3D9 reset invalidates resources independently, including while already in a menu. Failed creation keeps stereo disabled and retains the existing one-second backoff and three-attempt budget, including failures of the final allocation drain. Missing bridge/backbuffer data waits for recovery without consuming allocation attempts. A failed Present queue drain clears tracking validity and releases held actions/menu clicks, preventing stale controller input while submission is unavailable. Menu/pause cursor handling remains on the existing overlay path.

Graphics diagnostics report initial failures, changed failure details and recovery. Quiet mode suppresses unchanged repeated conditions per instance; **Verbose diagnostics** repeats unchanged details at most once every five seconds per condition. New failures, changed details and recovery remain immediate. Logs distinguish missing bridge/capture/image/dimensions/context, non-owned devices, recreation/reset and submission failures. A successful API call or allocation does not establish correct headset pixels.

Advanced offers these optional legacy HUD controls:

| Setting | Default | Effect |
| --- | --- | --- |
| `HUDInEyeCentered` | `false` | Centers in-game HUD panels within each eye. Requires `AimMode=0` or `1` and `ExperimentalHUDOverlay=false`. |
| `HUDInEyeScale` | `0.45` | Width as a fraction of the eye target; allowed range `0.2` to `1`. Aspect is retained and the rectangle must fit inside the eye. |
| `HUDInEyeVerticalOffset` | `0.05` | Vertical offset as a fraction of eye height; allowed range `-0.4` to `0.4`. Positive moves down. |
| `ShowSourceCrosshair` | `true` | `false` hides recognized flat Source crosshair sprites in legacy VR eye rendering. It preserves native blue/orange Portal status, menus, desktop drawing, controller aiming and the aim line. |

HUD centering changes only a validated eye viewport and restores the previous viewport on scope exit. It does not bind a different depth buffer. Mixed paint passes containing UI/cursor layers retain the original viewport. Incompatible centering settings are rejected by the launcher; the native parser falls back safely. The recommended native reticle/caption profile stays unchanged. Previously saved complete launcher preferences gain the new defaults without resetting existing values or paths; malformed/incomplete preferences remain errors.

The `native-vr` fork's controller grabbing was not adopted: its hand-origin override misses directly invoked `UpdateObjectVM`, detects holding without a verified local-player check, and writes ConVar fields through assumed offsets. Objects are still picked up with the portal gun's beam and `+use`.

`HeldObjectAnchor` (default on) instead works inside the verified `UpdateObject` scope: the object stays attached to the gun and turns with the wrist, while native code keeps doing portals, traces and collisions. Set it to `false` to restore Portal 2's own hold. Hold-distance adjustment is not implemented.

These changes require a fresh game/headset session. Test startup, repeated menu idling and returns, pause/resume, campaign/workshop map loads, a graphics reset while in a menu and in gameplay, controller loss/recovery, exit, and log recovery after a transient failure. Test optional HUD controls in both eyes with legacy aiming; confirm that the recommended native portal-status reticle, captions, menu pointer and aim line behave as before. A map switch that never exposes a non-game state needs separate observation; scene transitions alone do not prove every Source resource event.

OpenVR Vulkan submissions use the application's queue, so consumer detachment and producer queue draining are resource-ordering safeguards, not a compositor fence or proof across drivers. The modified DXVK/OpenVR resource lifetime remains a runtime compatibility risk. See [Valve's Vulkan integration guidance](https://github.com/ValveSoftware/openvr/wiki/Vulkan). No new hardware validation is claimed by this backport.

## Running from source

Requirements: the same as above, plus a local build. The launcher requires Python **3.10+** with Tcl/Tk (included in the standard Windows Python installer). It does not install Python, SteamVR or drivers automatically.

1. Build the mod as described below. This source checkout does not contain a prebuilt `Release/d3d9.dll`.
2. Double-click **`Portal 2 VR Enriched Launcher.cmd`** in the repository root. Alternatively run `python -m tools.launcher` or open `Portal 2 VR Enriched Launcher.pyw`. Run `python -m tools.launcher_shortcut` once for shortcuts with the launcher icon in the repository root and on the desktop; pin the desktop one to keep that icon on the taskbar.
3. Check the folder containing `portal2.exe` and your `steam.exe`; the launcher fills them in from Steam's registry entry and library list when the saved ones are missing. Choose the Portal 2 copy registered in that Steam client: launch requests AppID 620, not the selected executable directly. Paths are checked separately; multiple independent Steam installations are not automatically matched (a library on another drive is fine). Close Portal 2 before installing or changing its files.
4. Connect the headset and controllers, then click **Play in VR**; SteamVR starts with the game. The launcher applies the config, backs up originals and launches Steam app 620 with `-insecure` and the retained VR-compatible video arguments.
5. Enable subtitles in Portal 2's own options if you want captions.

## Building and verification

Use Visual Studio 2022 / Build Tools with the C++ desktop workload, MSVC v143 and a Windows SDK. Visual Studio 2026 also works: add `/p:PlatformToolset=v145` to each command. Build **Release / x86**; the game-facing DLL must remain 32-bit. DXVK, OpenVR and MinHook sources are included; no submodule initialization is required.

From a Developer Command Prompt in the repository root:

```bat
msbuild l4d2vr.sln /t:Build /p:Configuration=Release /p:Platform=x86 /p:PORTAL2_DIR=
msbuild tests\stabilization_tests.vcxproj /t:Build /p:Configuration=Release /p:Platform=Win32
tests\bin\stabilization_tests.exe
msbuild tests\pcvr_render_tests.vcxproj /t:Build /p:Configuration=Release /p:Platform=Win32
tests\bin\pcvr_render_tests.exe
msbuild tests\pcvr_hud_tests.vcxproj /t:Build /p:Configuration=Release /p:Platform=Win32
tests\bin\pcvr_hud_tests.exe
python -m unittest discover -s tests -p test_*.py
```

Output: `Release/d3d9.dll`. Building does not deploy unless `PORTAL2_DIR` is set; leave it empty when using the launcher so it owns backup/staging. Optional direct MSBuild deployment copies the DLL to `%PORTAL2_DIR%\bin` and **does not create a launcher backup**. No machine-specific Steam deployment path is embedded in the build.

C++/Python suites validate isolated logic and fixture installations. GUI tests use temporary game files and intercept external Steam process invocation; they do not launch Portal 2 or validate a headset session. Existing Source SDK/DXVK compiler warnings remain; a successful build is not runtime VR proof.

## Support

The mod is free and stays free. If you enjoy it and want to say thanks, you can [buy me a coffee on Ko-fi](https://ko-fi.com/oxenar7). It is entirely optional and unlocks nothing.

## Credits

This project stands on the work of many people. Thanks to:

- [sd805/l4d2vr](https://github.com/sd805/l4d2vr): the original Left 4 Dead 2 VR mod this code descends from.
- [Gistix/portal2vr](https://github.com/Gistix/portal2vr): the Portal 2 VR port.
- [mmadej20/portal2vr-ultimate](https://github.com/mmadej20/portal2vr-ultimate): the fork this one is built on (roomscale, launcher, test suite).
- [iFeelLikeChicken2Nite/portal2vr-ultimate](https://github.com/iFeelLikeChicken2Nite/portal2vr-ultimate): menu-refresh and HUD ideas, and the viewport centering in `eye_hud.h`.
- [fholger](https://github.com/fholger): the VR-modified DXVK that all of these use.
- [keyou91/l4d2vr](https://github.com/keyou91/l4d2vr) and [Hochgeschwindigkeitsrennfahrer/black-mesa-vr](https://github.com/Hochgeschwindigkeitsrennfahrer/black-mesa-vr): design references for frame submission and the DXVK upgrade.
- [stsichler/portal_reloaded_vr](https://github.com/stsichler/portal_reloaded_vr) and [BurntToast33/portal2vr_dxvk2.7.1](https://github.com/BurntToast33/portal2vr_dxvk2.7.1): references for upgrading DXVK.
- [DXVK](https://github.com/doitsujin/dxvk), [DXVK-GPLALL](https://github.com/Digger1955/dxvk-gplall), [MinHook](https://github.com/TsudaKageyu/minhook) and [OpenVR](https://github.com/ValveSoftware/openvr). Licenses and source pins are in `THIRD_PARTY_SOURCES.txt`.
- [CircuitLord](https://github.com/CircuitLord)'s VR Mod Installer inspired the launcher's look.

Gistix (portal2vr) and mmadej20 (portal2vr-ultimate) agreed to this fork being published, in November 2026.

## License

GPL-3.0, like [portal2vr](https://github.com/Gistix/portal2vr) and [portal2vr-ultimate](https://github.com/mmadej20/portal2vr-ultimate): see `COPYING`. DXVK, MinHook, OpenVR and the Source SDK headers keep their own licenses, listed in `THIRD_PARTY_SOURCES.txt`.

Portal 2 is a trademark of Valve Corporation.
