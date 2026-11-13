# EaglePatch+AC1

[![Build](https://github.com/Rodrigo-Matsuura/EaglePatch/actions/workflows/build.yml/badge.svg)](https://github.com/Rodrigo-Matsuura/EaglePatch/actions/workflows/build.yml)
[![Release](https://img.shields.io/github/v/release/Rodrigo-Matsuura/EaglePatch?color=blue)](https://github.com/Rodrigo-Matsuura/EaglePatch/releases)
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20(Proton)-lightgrey)](https://github.com/Rodrigo-Matsuura/EaglePatch)

EaglePatch+AC1 is an ASI plugin that fixes a number of issues in Assassin's Creed: Director's Cut Edition

**Plugin version:** 1.5.2

<img src="/assets/EaglePatch+AC1.png" alt="EaglePatch+AC1">

## Features
- Improved shadow map size from 1024 to 4096 (only when shadow quality is maxed out in game's settings; can be disabled in ini)
- Added high-precision framerate limiter (`FramerateLimit`) with optimized CPU sleep thresholds to prevent physics glitches on high refresh rate monitors (>60 FPS)
- Unlocked multisampling on high resolutions
- Added support of all XInput controllers, including PS4/PS5 controllers via standard wrappers/Steam Input (can be disabled in ini)
- Added dynamic 4-slot XInput auto-reconnect scanning for seamless controller hotplugging
- Robust controller hotplugging with null-safety protection (prevents crashes on controller disconnect/reconnect)
- Removed support of DInput controllers (can be disabled in ini; FPS limiter can remain active independently)
- Removed telemetry (improved bypass in version 1.2)
- Gamepad and kb/mouse controls work at the same time (read note below)
- Added ini setting for enabling PS3/PS4/PS5-like controls (swapping bumpers and triggers)
- Added ini setting to skip intro videos (without having to rename files)
- Added ini setting to limit CPU cores the game can use (fixes game crashing at startup when using a CPU with a high core count)
- Improved thread-safety in memory patching with instruction cache coherence (prevents random startup crashes)
- Full compatibility and cross-compilation support on Linux / Wine / Steam Deck (Proton)
- [DX10 only] Fixed doubling of resolution modes in settings
- Added optional diagnostic logging (`EnableLogging`) to troubleshoot startup and controller detection

Note: switching controls layout in game settings has no effect anymore, if you didn't use KeyboardMouse2 layout for keyboard, you need to change KeyboardLayout setting in EaglePatchAC1.ini

## Installation
1. Download latest release of EaglePatch+AC1.zip: https://github.com/Rodrigo-Matsuura/EaglePatch/releases
2. Download Ultimate ASI Loader: https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/ OBS: Download the Ultimate-ASI-Loader.zip
3. Put dinput8.dll from Ultimate ASI Loader into your Assassin's Creed: Director's Cut Edition directory
4. Extract the contents of EaglePatch+AC1.zip directly into your Assassin's Creed: Director's Cut Edition directory
5. Done! You may want to check scripts/EaglePatchAC1.ini for any settings you want to enable or disable

## Confirmed supported exes
#### AssassinsCreed_Dx9.exe (GOG/Steam)
CRC32: E8936C99  
MD5: 8E72C3333743780E43BC2C34BBF625F9  
SHA-1: A9EA476E70F076709CCA59A802E26EC894432120  

#### AssassinsCreed_Dx10.exe (GOG/Steam)
CRC32: 3AF8F9D0  
MD5: CA87753255E2D14B1F18BB737C643792  
SHA-1: 601475FD5EFA02BBAE80D1C06DFA25A0A1E7FD2F  

# EaglePatch+AC2

[![Build](https://github.com/Rodrigo-Matsuura/EaglePatch/actions/workflows/build.yml/badge.svg)](https://github.com/Rodrigo-Matsuura/EaglePatch/actions/workflows/build.yml)
[![Release](https://img.shields.io/github/v/release/Rodrigo-Matsuura/EaglePatch?color=blue)](https://github.com/Rodrigo-Matsuura/EaglePatch/releases)
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20(Proton)-lightgrey)](https://github.com/Rodrigo-Matsuura/EaglePatch)

EaglePatch+AC2 is an ASI plugin that fixes a number of issues in Assassin's Creed II

**Plugin version:** 1.5.2

<img src="/assets/EaglePatch+AC2.png" alt="EaglePatch+AC2">

## Features
- Improved shadow map size from 1024 to 4096 (only when shadow quality is maxed out in game's settings; can be disabled in ini)
- Configurable shadow cascade distances (`ShadowCascadeDistanceMultiplier`, default 6) to move the visible shadow-detail transitions farther away on the verified Steam Digital UPlay layout
- Draw distance maxed out for buildings, peds and flags (can be disabled in ini)
- Added high-precision framerate limiter (`FramerateLimit`) with optimized CPU sleep thresholds to prevent physics glitches on high refresh rate monitors (>60 FPS)
- Added borderless windowed mode option (`BorderlessWindow`) with frame refresh in ini (currently supported on AC2 Digital UPlay)
- Enables UPlay bonuses - Auditore Crypt, Altair robes, extra knives, bonus dye (can be disabled in ini)
- Added support of all XInput controllers, including PS4/PS5 controllers via standard wrappers/Steam Input (can be disabled in ini)
- Added dynamic 4-slot XInput auto-reconnect scanning for seamless controller hotplugging
- Robust controller hotplugging with null-safety protection (prevents crashes on controller disconnect/reconnect)
- Removed support of DInput controllers (can be disabled in ini; FPS limiter can remain active independently)
- Gamepad and kb/mouse controls work at the same time (read note below)
- Added ini setting for enabling PS3/PS4/PS5-like controls (swapping bumpers and triggers)
- Added ini setting to skip intro videos (without having to rename files)
- Added ini setting to limit CPU cores the game can use (fixes game crashing at startup when using a CPU with a high core count)
- Improved thread-safety in memory patching with instruction cache coherence (prevents random startup crashes)
- Full compatibility and cross-compilation support on Linux / Wine / Steam Deck (Proton)
- Added optional diagnostic logging (`EnableLogging`) to troubleshoot startup and controller detection

Note: switching controls layout in game settings has no effect anymore, if you didn't use KeyboardMouse2 layout for keyboard, you need to change KeyboardLayout setting in EaglePatchAC2.ini

## Installation
1. Download latest release of EaglePatch+AC2.zip: https://github.com/Rodrigo-Matsuura/EaglePatch/releases
2. Download Ultimate ASI Loader: https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/ OBS: Download the Ultimate-ASI-Loader.zip
3. Put dinput8.dll from Ultimate ASI Loader into your Assassin's Creed II directory
4. Extract the contents of EaglePatch+AC2.zip directly into your Assassin's Creed II directory
5. Done! You may want to check scripts/EaglePatchAC2.ini for any settings you want to enable or disable

## Shadow cascade distances

`ShadowCascadeDistanceMultiplier=6` in `scripts/EaglePatchAC2.ini` extends the three inner shadow cascade distances by up to 6x. Use an integer from 2 to 6, or 1 to restore the game's distances. On a map with stock splits of 4 / 16 / 64 / 400, 6x gives 24 / 96 / 384 / 400. The outer shadow limit stays unchanged; closer outer limits automatically reduce the multiplier to keep the cascades ordered. This moves the detail transitions farther away, but spreads each shadow map over more ground and does not remove the engine's cascade transitions.

This setting is independent of `ImproveShadowMapResolution` and the game's shadow quality. It does not change their settings. Set the game's shadows to High if you also enable the existing 4096 shadow-map option.

The hook requires the verified Steam Digital UPlay executable layout (PE timestamp `0x5925BA62`, image size `0x2190000`) and an exact runtime code signature. Unsupported or already-hooked layouts skip this feature while retaining the other EaglePatch fixes. Remove `scripts/AC2ShadowRadius.asi` when upgrading from the standalone patch so only EaglePatch owns the hook. With `EnableLogging=1`, the log records installation or skip reasons and the first observed split distances when the plugin shuts down.

## Supported game versions
- Digital UPlay
- Retail Akella v1.01

These plugins are using a lightweight win32 patcher by withmorten: https://github.com/withmorten/patcher

---

This project is a fork of the original work by Sergeanur: https://github.com/Sergeanur/EaglePatch

**Project Maintainer (Fork):**
Github: https://github.com/Rodrigo-Matsuura/
