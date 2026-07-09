# 🐍 PSP Snake

A classic Snake game written in C for the **Sony PSP** running **Custom Firmware (PRO/ME/LME)**.

---

## Controls

| Button | Action |
|--------|--------|
| D-Pad ↑↓←→ | Steer the snake |
| **START** | Pause / Resume |
| **X (Cross)** | Restart after Game Over |
| **HOME** | Exit to XMB |

## Gameplay

- Guide the snake to eat the **pulsing red food**
- Each piece of food grows the snake by 1 and adds 1 point to your score
- **Every 5 points** the snake speeds up
- Hitting a wall or yourself ends the game
- Your **best score** is kept for the session

---

## Build Requirements

| Tool | Purpose |
|------|---------|
| [Docker Desktop](https://www.docker.com/products/docker-desktop) | Provides the PSPSDK build environment |
| Windows 10/11 | Host OS |

The `pspdev/pspdev` Docker image contains the full PSP cross-compiler toolchain.
No native installation of PSPSDK required.

---

## Building

Open **PowerShell** in the `PSP_lab` folder and run:

```powershell
.\build.ps1
```

The script will:
1. Start Docker Desktop if it is not running
2. Pull the `pspdev/pspdev` image (first run only, ~1–2 GB)
3. Compile the game inside the container
4. Output `snake\EBOOT.PBP`
5. Copy `EBOOT.PBP` to `PSP\GAME\Snake\` (mirrors your memory stick layout)

---

## Deploying to PSP

1. Connect your PSP via USB **or** remove the memory stick and use a card reader
2. On the memory stick, create the folder:
   ```
   ms0:\PSP\GAME\Snake\
   ```
3. Copy `snake\EBOOT.PBP` into that folder
4. Eject / disconnect
5. On the PSP: **Game → Memory Stick → Snake**

> **Requires Custom Firmware**: PRO-C, ME, or LME.  
> Does NOT work on official Sony firmware.

---

## Project Structure

```
PSP_lab/
├── build.ps1           ← Run this to build
├── snake/
│   ├── main.c          ← Full game source
│   ├── Makefile        ← PSP SDK Makefile
│   └── ICON0.PNG       ← Game icon shown in XMB (144×80 px)
└── PSP/
    └── GAME/
        └── Snake/
            └── EBOOT.PBP  ← Built output (deploy this)
```

---

## Technical Notes

- **Language**: C (C99)
- **SDK**: [PSPSDK / pspdev](https://github.com/pspdev/pspdev)
- **Display**: Direct framebuffer writes to PSP VRAM (`0x44000000`, uncached)
- **Text**: `pspDebugScreen` for score/overlays
- **Timing**: `sceRtc` for frame-independent snake speed
- **Resolution**: 480×272 (native PSP)
- **Grid**: 30×16 cells (16 px each), with a 16 px header for the score bar

---

## License

MIT — free to modify and share.
