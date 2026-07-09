# 🎮 PSP Lab: Homebrew Projects

A collection of custom homebrew applications built for the **Sony PlayStation Portable (PSP)** running **Custom Firmware (PRO/ME/LME)**.

This repository is split between two main homebrew projects:
1. **🐍 PSP Snake (Stable Release)**: An optimized, double-buffered classic Snake game utilizing the `pspgu` hardware engine.
2. **🎵 PSPify (Spotify Clone DEMO - Active WIP)**: A retro-modern Spotify desktop player clone for local MP3 playback, featuring a Y2K vaporwave skin, hardware-accelerated Media Engine decoding, and folder-based album art parsing.

---

## 🛠️ Build & SDK Environment

All compilation is handled via **Docker** running the official PSPSDK toolchain. No local SDK configuration is required on the host system.

| Tool | Purpose |
|------|---------|
| [Docker Desktop](https://www.docker.com/products/docker-desktop) | Runs the compiler container |
| Windows PowerShell | Execution shell |

---

## 🐍 1. PSP Snake (Stable)

An optimized clone of the classic Snake game running at a smooth 60 FPS. It utilizes double-buffered VRAM switching and hardware sprites to eliminate CPU screen tearing.

### 🎮 Controls
* **D-Pad**: Steer the snake
* **START**: Pause / Resume game
* **Cross (X)**: Restart game (after Game Over)
* **HOME**: Exit to the PSP Dashboard (XMB)

### 🚀 Build & Run
1. Run the Snake build script:
   ```powershell
   .\build.ps1
   ```
2. Copy `snake/EBOOT.PBP` to `ms0:\PSP\GAME\Snake\EBOOT.PBP`.

---

## 🎵 2. PSPify (Spotify Clone DEMO)

> [!IMPORTANT]
> **⚠️ ACTIVE DEMO & WORK IN PROGRESS**
> This application is currently in active development. Features are subject to expand as the hardware decoder integration, user interface, and folder parsing are optimized.

Recreates the late 2000s / early 2010s Spotify desktop app aesthetic on the PSP screen (480x272), styled with a retro Y2K vaporwave color palette (neon pink, cyan, and deep indigo grid lines).

### 🌟 Features
* **ME Hardware Decoder**: Decodes MP3s natively on the PSP's auxiliary **Media Engine** processor (`sceMp3` / `avcodec`), freeing the main CPU.
* **Y2K Grid Aesthetics**: Features double-bordered frame widgets, checkered grid lines, and classic web-badges (`[WINAMP]`, `[NETSCAPE]`).
* **Folder Cover Art**: Automatically searches the song's parent directory for files like `cover.jpg` or `cover.png` and decompresses them using `stb_image.h` to display a 64x64 album art thumbnail.
* **Equalizer & Vol**: A 12-channel animated visualizer and dynamic progress bar / volume level controls.

### 🎮 Controls
* **D-Pad Up/Down**: Navigate the song lists
* **Cross (X)**: Play selected track (or select playlist focus)
* **Triangle (△)**: Pause / Resume playback
* **Square (□)**: Stop song
* **Left / Right Trigger**: Play Previous / Next track
* **L/R Trigger (Hold) + D-Pad Left/Right**: Adjust playback volume

### 🚀 Build & Run
1. Run the PSPify build script:
   ```powershell
   .\build_pspify.ps1
   ```
2. Copy `pspify/EBOOT.PBP` to `ms0:\PSP\GAME\PSPify\EBOOT.PBP`.
3. Put your MP3s in `ms0:\MUSIC\`.
4. Put the cover art image (named `cover.jpg` or `cover.png`) in the same folder as the songs.

---

## 📂 Project Structure

```text
PSP_lab/
├── build.ps1             ← Compile script for Snake
├── build_pspify.ps1      ← Compile script for PSPify
├── README.md             ← This documentation hub
├── snake/                ← Snake App Folder
│   ├── main.c            ← Snake game source
│   ├── Makefile          ← PSPSDK compilation rule
│   └── ICON0.PNG         ← Snake XMB icon
├── pspify/               ← PSPify App Folder
│   ├── main.c            ← Spotify player source
│   ├── Makefile          ← PSPSDK compilation rule
│   ├── font8x8.h         ← Basic ASCII pixel font sheet
│   ├── stb_image.h       ← Header-only JPEG/PNG loader
│   └── ICON0.PNG         ← Spotify XMB icon
└── PSP/
    └── GAME/             ← Compiled EBOOT distribution outputs
        ├── Snake/EBOOT.PBP
        └── PSPify/EBOOT.PBP
```

---

## ⚖️ License

MIT - Free to modify and share.
