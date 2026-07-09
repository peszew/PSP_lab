/*
 * ============================================================
 *  PSPify - Spotify Music Player for PSP Custom Firmware
 * ============================================================
 *  Retro-modern dark interface, matching the 2010s Spotify player.
 *  Uses the hardware Media Engine for decoding and hardware GU
 *  for drawing. Loads local album artwork via stb_image.h.
 *
 *  Controls
 *    D-Pad Up/Down    Navigate playlist
 *    Cross (X)        Play selected song
 *    Triangle (Tr)    Pause / Resume
 *    Square (Sq)      Stop playback
 *    Right Trigger    Next song
 *    Left Trigger     Previous song
 *    D-Pad L/R        Adjust app volume
 *    HOME             Exit to XMB
 * ============================================================
 */
#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspdebug.h>
#include <pspctrl.h>
#include <psprtc.h>
#include <pspgu.h>
#include <pspge.h>
#include <pspaudio.h>
#include <pspmp3.h>
#include <pspiofilemgr.h>
#include <psputility.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <strings.h>

/* Define stb_image config before including it */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#include "stb_image.h"
#include "font8x8.h"

/* Global font texture buffer */
static u32 __attribute__((aligned(16))) fontTex[128 * 128];

PSP_MODULE_INFO("PSPify", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);
PSP_HEAP_SIZE_KB(16384); /* 16 MB heap for stb_image decodes */

/* ============================================================
 *  Constants
 * ============================================================ */
#define SCR_W       480
#define SCR_H       272
#define VRAM_W      512
#define FBSIZE      (VRAM_W * SCR_H * 4)

#define MAX_SONGS   200
#define COVER_SIZE  64

/* Playback States */
#define PLAY_STATE_STOPPED  0
#define PLAY_STATE_PLAYING  1
#define PLAY_STATE_PAUSED   2

/* Sidebar Categories */
#define MENU_LOCAL_FILES    0
#define MENU_NOW_PLAYING    1
#define MENU_SETTINGS       2

/* ============================================================
 *  Colors (ABGR: 0xAABBGGRR)
 * ============================================================ */
#define C_BG           0xFF121212u  /* Matte black                       */
#define C_SPOTIFY_G    0xFF1DB954u  /* Spotify Neon Green                */
#define C_TEXT_W       0xFFFFFFFFu  /* Bright white                      */
#define C_TEXT_G       0xFF888888u  /* Muted grey                        */
#define C_BORDER       0xFF282828u  /* Subtle line divider               */
#define C_SELECT_BG    0xFF222222u  /* Highlighted row background        */
#define C_BAR_BG       0xFF404040u  /* Progress track background         */
#define C_WHITE_TR     0x44FFFFFFu  /* Semi-transparent white            */

/* NeoCities / Vaporwave Vibe Colors */
#define C_VAPOR_BG     0xFF14050Au  /* Checkered grid background dark  */
#define C_VAPOR_GRID   0xFF24101Au  /* Grid line color                 */
#define C_PANEL_SIDE   0xFF210A15u  /* Sidebar panel fill              */
#define C_PANEL_MAIN   0xFF14050Du  /* Main tracks panel fill          */
#define C_PANEL_PLAY   0xFF2A0A1Au  /* Player bottom panel fill        */
#define C_VAPOR_CYAN   0xFFFFFF00u  /* Neon Cyan                       */
#define C_VAPOR_PINK   0xFFFF00FFu  /* Neon Pink                       */

/* ============================================================
 *  Types
 * ============================================================ */
typedef struct {
    char path[160];
    char name[64];
    unsigned int size;
} Song;

typedef struct { short x, y, z; } Vtx;
typedef struct { short u, v; u32 color; short x, y, z; } TexVtx;

/* ============================================================
 *  Globals
 * ============================================================ */
static unsigned int __attribute__((aligned(16))) dispList[65536];
static void* drawBuf = (void*)0;
static void* dispBuf = (void*)FBSIZE;

/* Cover Art texture cache (aligned to 16 bytes for hardware DMA) */
static u32 __attribute__((aligned(16))) coverTex[COVER_SIZE * COVER_SIZE];
static int hasCover = 0;

/* Lists and indices */
static Song songs[MAX_SONGS];
static int songCount = 0;
static int selectedSong = 0;
static int scrollOffset = 0;
static int activeMenu = MENU_LOCAL_FILES;
static int sidebarFocused = 1; /* Focus starts on sidebar menu */

/* Volume level (ranges from 0 to 32768, standard in pspaudio) */
static int playVolume = 24576; // Default to ~75%

/* Player states & variables (shared with audio thread) */
static volatile int playState = PLAY_STATE_STOPPED;
static int activeTrackIndex = -1;
static char activeTrackPath[160] = {0};
static char activeTrackName[64] = {0};

static volatile float elapsedSeconds = 0.0f;

static volatile int totalDuration = 0;
static volatile int mp3Samplerate = 44100;
static volatile int mp3Bitrate = 128;
static SceUID playThreadID = -1;

static int debugAvcodecLoadRes = -999;
static int debugMp3LoadRes = -999;
static int debugFileOpenRes = -999;
static int debugMp3ReserveRes = -999;
static int debugMp3InitRes = -999;
static int debugDecodeRes = -999;
static char debugLastOpenedPath[160] = "None";


/* MP3 decoder static buffers to prevent heap fragmentation */
#define MP3_BUF_SIZE (64 * 1024)
#define PCM_BUF_SIZE (32 * 1024)
static u8 mp3Buf[MP3_BUF_SIZE] __attribute__((aligned(64)));
static s16 pcmBuf[PCM_BUF_SIZE] __attribute__((aligned(64)));

/* Visualizer dynamic states */
static float barHeights[12] = {0};
static float barTargets[12] = {0};

/* ============================================================
 *  GU Helpers
 * ============================================================ */
static void guRect(int x, int y, int w, int h, u32 color)
{
    sceGuColor(color);
    Vtx* v = (Vtx*)sceGuGetMemory(2 * sizeof(Vtx));
    if (!v) return;
    v[0].x = (short)x;       v[0].y = (short)y;       v[0].z = 0;
    v[1].x = (short)(x + w); v[1].y = (short)(y + h); v[1].z = 0;
    sceGuDrawArray(GU_SPRITES, GU_VERTEX_16BIT | GU_TRANSFORM_2D, 2, 0, v);
}

static void drawTexRect(int x, int y, int w, int h)
{
    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexMode(GU_PSM_8888, 0, 0, 0);
    sceGuTexImage(0, COVER_SIZE, COVER_SIZE, COVER_SIZE, coverTex);
    sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGBA);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);

    TexVtx* v = (TexVtx*)sceGuGetMemory(2 * sizeof(TexVtx));
    if (!v) return;
    v[0].u = 0;          v[0].v = 0;          v[0].color = 0xFFFFFFFF; v[0].x = x;     v[0].y = y;     v[0].z = 0;
    v[1].u = COVER_SIZE; v[1].v = COVER_SIZE; v[1].color = 0xFFFFFFFF; v[1].x = x + w; v[1].y = y + h; v[1].z = 0;

    sceGuDrawArray(GU_SPRITES, GU_TEXTURE_16BIT | GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 2, 0, v);
    sceGuDisable(GU_TEXTURE_2D);
}

static void initFont(void)
{
    memset(fontTex, 0, sizeof(fontTex));
    for (int i = 0; i < 128; i++) {
        int cx = i % 16;
        int cy = i / 16;
        for (int y = 0; y < 8; y++) {
            u8 row = (u8)font8x8_basic[i][y];
            for (int x = 0; x < 8; x++) {
                if ((row >> x) & 1) {
                    fontTex[(cy * 8 + y) * 128 + (cx * 8 + x)] = 0xFFFFFFFF;
                } else {
                    fontTex[(cy * 8 + y) * 128 + (cx * 8 + x)] = 0x00000000;
                }
            }
        }
    }
    sceKernelDcacheWritebackAll();
}

static void drawChar(int x, int y, char c, u32 color)
{
    int idx = (unsigned char)c;
    if (idx >= 128) idx = '?';
    int cx = idx % 16;
    int cy = idx / 16;
    int u = cx * 8;
    int v = cy * 8;

    TexVtx* verts = (TexVtx*)sceGuGetMemory(2 * sizeof(TexVtx));
    if (!verts) return;

    verts[0].u = u;     verts[0].v = v;     verts[0].color = color; verts[0].x = x;     verts[0].y = y;     verts[0].z = 0;
    verts[1].u = u + 8; verts[1].v = v + 8; verts[1].color = color; verts[1].x = x + 8; verts[1].y = y + 8; verts[1].z = 0;

    sceGuDrawArray(GU_SPRITES, GU_TEXTURE_16BIT | GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 2, 0, verts);
}

static void drawStr(int x, int y, const char *str, u32 color)
{
    if (!str || *str == '\0') return;
    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexMode(GU_PSM_8888, 0, 0, 0);
    sceGuTexImage(0, 128, 128, 128, fontTex);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexFilter(GU_NEAREST, GU_NEAREST);
    
    int curX = x;
    while (*str) {
        drawChar(curX, y, *str, color);
        curX += 8;
        str++;
    }
    sceGuDisable(GU_TEXTURE_2D);
}

static void initGU(void)
{
    sceGuInit();
    sceGuStart(GU_DIRECT, dispList);
    sceGuDrawBuffer(GU_PSM_8888, drawBuf, VRAM_W);
    sceGuDispBuffer(SCR_W, SCR_H, dispBuf, VRAM_W);
    sceGuDepthBuffer((void*)(FBSIZE * 2), VRAM_W);

    sceGuOffset(2048 - SCR_W / 2, 2048 - SCR_H / 2);
    sceGuViewport(2048, 2048, SCR_W, SCR_H);
    sceGuDepthRange(65535, 0);
    sceGuScissor(0, 0, SCR_W, SCR_H);
    sceGuEnable(GU_SCISSOR_TEST);

    sceGuDisable(GU_DEPTH_TEST);
    sceGuShadeModel(GU_FLAT);
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_LIGHTING);
    sceGuDisable(GU_CULL_FACE);
    
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    
    sceGuDisable(GU_ALPHA_TEST);

    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);
}

/* ============================================================
 *  Directory Scanner
 * ============================================================ */
static void scanMusicDir(const char *dirPath)
{
    SceUID dfd = sceIoDopen(dirPath);
    if (dfd < 0) return;

    SceIoDirent entry;
    while (sceIoDread(dfd, &entry) > 0) {
        if (entry.d_name[0] == '.') continue;

        if (entry.d_stat.st_attr & FIO_SO_IFDIR) {
            /* Scan folder 1 level deep */
            char subPath[256];
            snprintf(subPath, sizeof(subPath), "%s/%s", dirPath, entry.d_name);
            SceUID subDfd = sceIoDopen(subPath);
            if (subDfd >= 0) {
                SceIoDirent subEntry;
                while (sceIoDread(subDfd, &subEntry) > 0) {
                    if (subEntry.d_name[0] == '.') continue;
                    int len = strlen(subEntry.d_name);
                    if (len > 4 && strcasecmp(subEntry.d_name + len - 4, ".mp3") == 0) {
                        if (songCount < MAX_SONGS) {
                            snprintf(songs[songCount].path, sizeof(songs[songCount].path), "%s/%s", subPath, subEntry.d_name);
                            snprintf(songs[songCount].name, sizeof(songs[songCount].name), "%s", subEntry.d_name);
                            songs[songCount].size = subEntry.d_stat.st_size;
                            songCount++;
                        }
                    }
                }
                sceIoDclose(subDfd);
            }
        } else {
            /* Read flat file */
            int len = strlen(entry.d_name);
            if (len > 4 && strcasecmp(entry.d_name + len - 4, ".mp3") == 0) {
                if (songCount < MAX_SONGS) {
                    snprintf(songs[songCount].path, sizeof(songs[songCount].path), "%s/%s", dirPath, entry.d_name);
                    snprintf(songs[songCount].name, sizeof(songs[songCount].name), "%s", entry.d_name);
                    songs[songCount].size = entry.d_stat.st_size;
                    songCount++;
                }
            }
        }
    }
    sceIoDclose(dfd);
}

/* ============================================================
 *  Cover Art Loading (stb_image.h)
 * ============================================================ */
static void loadCoverForSong(const char *songPath)
{
    hasCover = 0;
    char folder[256];
    strncpy(folder, songPath, sizeof(folder));
    char *lastSlash = strrchr(folder, '/');
    if (!lastSlash) lastSlash = strrchr(folder, '\\');
    
    if (lastSlash) {
        *lastSlash = '\0';
    } else {
        strcpy(folder, "ms0:/MUSIC");
    }

    /* Try common cover art names */
    const char *names[] = { "cover.jpg", "cover.png", "folder.jpg", "folder.png", "Cover.jpg", "Folder.jpg" };
    for (int i = 0; i < 6; i++) {
        char imgPath[300];
        snprintf(imgPath, sizeof(imgPath), "%s/%s", folder, names[i]);

        SceUID fd = sceIoOpen(imgPath, PSP_O_RDONLY, 0777);
        if (fd >= 0) {
            sceIoClose(fd);

            int w, h, channels;
            unsigned char *data = stbi_load(imgPath, &w, &h, &channels, 4); /* Force RGBA */
            if (data) {
                /* Scale and copy pixels into aligned 64x64 buffer */
                for (int y = 0; y < COVER_SIZE; y++) {
                    int srcY = (y * h) / COVER_SIZE;
                    for (int x = 0; x < COVER_SIZE; x++) {
                        int srcX = (x * w) / COVER_SIZE;
                        unsigned char *pixel = data + (srcY * w + srcX) * 4;
                        /* Convert RGBA to ABGR */
                        coverTex[y * COVER_SIZE + x] = ((u32)pixel[3] << 24) | ((u32)pixel[2] << 16) | ((u32)pixel[1] << 8) | pixel[0];
                    }
                }
                stbi_image_free(data);
                hasCover = 1;
                break;
            }
        }
    }
}

/* ============================================================
 *  Audio Player Loop
 * ============================================================ */
static void fillMp3Buffer(SceUID fd, int handle)
{
    u8 *dst;
    SceInt32 towrite;
    SceInt32 srcpos;

    if (sceMp3CheckStreamDataNeeded(handle) > 0) {
        if (sceMp3GetInfoToAddStreamData(handle, &dst, &towrite, &srcpos) == 0) {
            sceIoLseek(fd, srcpos, SEEK_SET);
            int read = sceIoRead(fd, dst, towrite);
            if (read > 0) {
                sceMp3NotifyAddStreamData(handle, read);
            } else {
                /* EOF or error: stop playback */
                playState = PLAY_STATE_STOPPED;
            }
        }
    }
}

static SceOff getMp3StreamStart(SceUID fd)
{
    sceIoLseek(fd, 0, SEEK_SET);
    u8 header[10];
    int read = sceIoRead(fd, header, 10);
    if (read == 10 && header[0] == 'I' && header[1] == 'D' && header[2] == '3') {
        /* Synchsafe integer size parsing (7 bits per byte) */
        u32 size = ((u32)header[6] << 21) | 
                   ((u32)header[7] << 14) | 
                   ((u32)header[8] << 7)  | 
                   (u32)header[9];
        return 10 + size;
    }
    return 0;
}

static int playThread(SceSize args, void *argp)
{
    debugFileOpenRes = sceIoOpen(activeTrackPath, PSP_O_RDONLY, 0777);
    strncpy(debugLastOpenedPath, activeTrackPath, sizeof(debugLastOpenedPath));
    if (debugFileOpenRes < 0) {
        playState = PLAY_STATE_STOPPED;
        return 0;
    }

    SceOff fileSize = sceIoLseek(debugFileOpenRes, 0, SEEK_END);
    sceIoLseek(debugFileOpenRes, 0, SEEK_SET);

    SceOff streamStart = getMp3StreamStart(debugFileOpenRes);

    /* Setup init arguments */
    SceMp3InitArg arg;
    memset(&arg, 0, sizeof(arg));
    arg.mp3StreamStart = streamStart;
    arg.mp3StreamEnd = fileSize;
    arg.mp3Buf = mp3Buf;
    arg.mp3BufSize = MP3_BUF_SIZE;
    arg.pcmBuf = (SceUChar8*)pcmBuf;
    arg.pcmBufSize = PCM_BUF_SIZE;

    debugMp3ReserveRes = sceMp3ReserveMp3Handle(&arg);
    if (debugMp3ReserveRes < 0) {
        sceIoClose(debugFileOpenRes);
        playState = PLAY_STATE_STOPPED;
        return 0;
    }

    /* Pre-fill buffer then initialize */
    fillMp3Buffer(debugFileOpenRes, debugMp3ReserveRes);
    debugMp3InitRes = sceMp3Init(debugMp3ReserveRes);
    if (debugMp3InitRes < 0) {
        sceMp3ReleaseMp3Handle(debugMp3ReserveRes);
        sceIoClose(debugFileOpenRes);
        playState = PLAY_STATE_STOPPED;
        return 0;
    }

    int handle = debugMp3ReserveRes;
    int fd = debugFileOpenRes;

    mp3Samplerate = sceMp3GetSamplingRate(handle);
    int channels = sceMp3GetMp3ChannelNum(handle);
    mp3Bitrate = sceMp3GetBitRate(handle);


    if (mp3Samplerate <= 0) mp3Samplerate = 44100;
    if (channels <= 0) channels = 2;
    if (mp3Bitrate <= 0) mp3Bitrate = 128;

    /* Calculate total duration in seconds */
    totalDuration = (int)((fileSize * 8) / (mp3Bitrate * 1000));

    /* Reserve hardware audio channel (1152 samples matches MP3 frame size) */
    int audioChannel = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, 1152, PSP_AUDIO_FORMAT_STEREO);
    if (audioChannel < 0) {
        sceMp3ReleaseMp3Handle(handle);
        sceIoClose(fd);
        playState = PLAY_STATE_STOPPED;
        return 0;
    }

    while (playState != PLAY_STATE_STOPPED) {
        if (playState == PLAY_STATE_PAUSED) {
            sceKernelDelayThread(20000);
            continue;
        }

        if (sceMp3CheckStreamDataNeeded(handle) > 0) {
            fillMp3Buffer(fd, handle);
        }

        short *pcmOutput;
        int decodedBytes = sceMp3Decode(handle, &pcmOutput);
        debugDecodeRes = decodedBytes;
        if (decodedBytes < 0) {
            /* Decode error or EOF */
            break;
        }

        if (decodedBytes > 0) {
            sceAudioOutputPannedBlocking(audioChannel, playVolume, playVolume, pcmOutput);
            elapsedSeconds += 1152.0f / (float)mp3Samplerate;
        } else {
            sceKernelDelayThread(5000);
        }
    }

    /* Cleanup */
    sceAudioChRelease(audioChannel);
    sceMp3ReleaseMp3Handle(handle);
    sceIoClose(fd);

    /* If finished naturally without being manually stopped, go to next song */
    if (playState == PLAY_STATE_PLAYING) {
        playState = PLAY_STATE_STOPPED;
        sceKernelDelayThread(100000); /* Delay before loading next */
        int nextIndex = activeTrackIndex + 1;
        if (nextIndex < songCount) {
            /* Start next song in chain */
            sceCtrlReadBufferPositive(NULL, 1); /* Refresh controller status */
            /* Post thread startup will be triggered by main loop */
            activeTrackIndex = nextIndex;
            snprintf(activeTrackPath, sizeof(activeTrackPath), "%s", songs[nextIndex].path);
            snprintf(activeTrackName, sizeof(activeTrackName), "%s", songs[nextIndex].name);
            loadCoverForSong(activeTrackPath);
            elapsedSeconds = 0.0f;
            totalDuration = 0;
            playState = PLAY_STATE_PLAYING;
            playThreadID = sceKernelCreateThread("mp3_player_thread", playThread, 0x10, 128 * 1024, 0, NULL);
            if (playThreadID >= 0) sceKernelStartThread(playThreadID, 0, NULL);
        }
    } else {
        playState = PLAY_STATE_STOPPED;
    }

    return 0;
}

static void startSong(int index)
{
    if (index < 0 || index >= songCount) return;

    /* Stop current thread if it exists */
    playState = PLAY_STATE_STOPPED;
    sceKernelDelayThread(80000);

    activeTrackIndex = index;
    snprintf(activeTrackPath, sizeof(activeTrackPath), "%s", songs[index].path);
    snprintf(activeTrackName, sizeof(activeTrackName), "%s", songs[index].name);

    loadCoverForSong(activeTrackPath);

    elapsedSeconds = 0.0f;
    totalDuration = 0;
    playState = PLAY_STATE_PLAYING;

    playThreadID = sceKernelCreateThread("mp3_player_thread", playThread, 0x10, 128 * 1024, 0, NULL);
    if (playThreadID >= 0) {
        sceKernelStartThread(playThreadID, 0, NULL);
    }
}



/* ============================================================
 *  UI Update & Drawing
 * ============================================================ */
static void updateVisualizer(void)
{
    for (int i = 0; i < 12; i++) {
        if (playState == PLAY_STATE_PLAYING) {
            if (rand() % 3 == 0) {
                barTargets[i] = (float)(rand() % 24 + 3);
            }
            barHeights[i] += (barTargets[i] - barHeights[i]) * 0.25f;
        } else {
            barHeights[i] += (0.0f - barHeights[i]) * 0.15f;
        }
    }
}

static void drawRetroPanel(int x, int y, int w, int h, u32 bgCol)
{
    /* Background fill */
    guRect(x, y, w, h, bgCol);

    /* Cyberpunk/Y2K double borders */
    /* Outer border (Neon Pink) */
    guRect(x, y, w, 1, C_VAPOR_PINK);         /* Top */
    guRect(x, y + h - 1, w, 1, C_VAPOR_PINK); /* Bottom */
    guRect(x, y, 1, h, C_VAPOR_PINK);         /* Left */
    guRect(x + w - 1, y, 1, h, C_VAPOR_PINK); /* Right */

    /* Inner border (Neon Cyan) */
    guRect(x + 2, y + 2, w - 4, 1, C_VAPOR_CYAN);         /* Top */
    guRect(x + 2, y + h - 3, w - 4, 1, C_VAPOR_CYAN); /* Bottom */
    guRect(x + 2, y + 2, 1, h - 4, C_VAPOR_CYAN);         /* Left */
    guRect(x + w - 3, y + 2, 1, h - 4, C_VAPOR_CYAN); /* Right */
}

static void render(void)
{
    /* Phase 1: GU Hardware Drawing */
    sceGuStart(GU_DIRECT, dispList);
    sceGuClearColor(C_VAPOR_BG);
    sceGuClear(GU_COLOR_BUFFER_BIT);

    /* Draw Checkered Grid background */
    for (int x = 0; x < SCR_W; x += 16) {
        guRect(x, 0, 1, SCR_H, C_VAPOR_GRID);
    }
    for (int y = 0; y < SCR_H; y += 16) {
        guRect(0, y, SCR_W, 1, C_VAPOR_GRID);
    }

    /* Left Sidebar Panel (Aligned to multiples of 8: x=8, y=8, w=104, h=200) */
    drawRetroPanel(8, 8, 104, 200, C_PANEL_SIDE);

    /* Main Tracks Panel (Aligned to multiples of 8: x=120, y=8, w=352, h=200) */
    drawRetroPanel(120, 8, 352, 200, C_PANEL_MAIN);

    /* Sidebar texts rendering */
    drawStr(20, 20, "PSPify", C_VAPOR_CYAN);
    drawStr(20, 36, "  Library", C_TEXT_G);
    
    drawStr(20, 56, " Local Files", (activeMenu == MENU_LOCAL_FILES) ? C_VAPOR_PINK : C_TEXT_W);
    drawStr(20, 72, " Now Playing", (activeMenu == MENU_NOW_PLAYING) ? C_VAPOR_PINK : C_TEXT_W);
    drawStr(20, 88, " Settings", (activeMenu == MENU_SETTINGS) ? C_VAPOR_PINK : C_TEXT_W);

    /* Sidebar Y2K Retro Badges */
    drawStr(20, 140, "[WINAMP]", C_VAPOR_CYAN);
    drawStr(20, 156, "[NETSCAPE]", C_VAPOR_PINK);
    drawStr(20, 172, "[60 FPS]", C_TEXT_G);

    if (activeMenu == MENU_LOCAL_FILES) {
        drawStr(136, 20, "Local Files", C_VAPOR_CYAN);
        drawStr(136, 36, "#   Title             Size", C_TEXT_G);
        
        /* Dotted divider line under header */
        guRect(136, 48, 320, 1, C_BORDER);

        if (songCount == 0) {
            drawStr(144, 70, "No music found in memory stick!", C_TEXT_G);
            drawStr(136, 86, "Copy MP3 files to ms0:/MUSIC/.", C_TEXT_G);
        } else {
            int drawCount = songCount - scrollOffset;
            if (drawCount > 9) drawCount = 9;

            for (int i = 0; i < drawCount; i++) {
                int songIdx = scrollOffset + i;
                int yPos = 56 + i * 16;

                /* Highlight background for selected track */
                if (songIdx == selectedSong) {
                    if (!sidebarFocused) {
                        guRect(125, yPos - 2, 310, 15, 0x44FFFF00u); /* Focused: Cyan fill */
                    } else {
                        guRect(125, yPos - 2, 310, 15, C_SELECT_BG);   /* Unfocused: Dark grey */
                    }
                }

                u32 rowCol = C_TEXT_W;
                if (songIdx == selectedSong && !sidebarFocused) {
                    rowCol = C_VAPOR_CYAN;
                } else if (songIdx == activeTrackIndex && playState != PLAY_STATE_STOPPED) {
                    rowCol = C_VAPOR_PINK;
                }

                /* Index number */
                char idxStr[8];
                snprintf(idxStr, sizeof(idxStr), "%02d", songIdx + 1);
                drawStr(136, yPos, idxStr, rowCol);

                /* Truncate track filename to fit neatly */
                char truncName[23];
                strncpy(truncName, songs[songIdx].name, sizeof(truncName));
                truncName[22] = '\0';
                char *dot = strrchr(truncName, '.');
                if (dot) *dot = '\0';
                drawStr(168, yPos, truncName, rowCol);

                /* Size in MB */
                char sizeStr[16];
                float mb = (float)songs[songIdx].size / (1024.0f * 1024.0f);
                snprintf(sizeStr, sizeof(sizeStr), "%4.1f MB", mb);
                drawStr(380, yPos, sizeStr, rowCol);
            }
        }

        /* Scrollbar track */
        if (songCount > 9) {
            guRect(446, 56, 4, 140, C_PANEL_SIDE);
            int barHeight = (9 * 140) / songCount;
            int barY = 56 + (scrollOffset * 140) / songCount;
            guRect(446, barY, 4, barHeight, C_VAPOR_CYAN);
        }
    } else if (activeMenu == MENU_NOW_PLAYING) {
        drawStr(136, 20, "Now Playing", C_VAPOR_CYAN);

        /* Spotify cover art in center screen */
        drawRetroPanel(140, 48, 104, 104, C_PANEL_SIDE);
        if (playState != PLAY_STATE_STOPPED) {
            if (hasCover) {
                drawTexRect(142, 50, 100, 100);
            } else {
                guRect(142, 50, 100, 100, C_VAPOR_PINK);
            }

            drawStr(260, 56, "Track:", C_TEXT_W);
            
            char shortName[17];
            strncpy(shortName, activeTrackName, sizeof(shortName));
            shortName[16] = '\0';
            char *dot = strrchr(shortName, '.');
            if (dot) *dot = '\0';
            drawStr(260, 70, shortName, C_VAPOR_CYAN);

            drawStr(260, 96, "Codec: MP3", C_TEXT_G);
            
            char samplerateStr[32];
            snprintf(samplerateStr, sizeof(samplerateStr), "Rate:  %d Hz", mp3Samplerate);
            drawStr(260, 110, samplerateStr, C_TEXT_G);

            char bitrateStr[32];
            snprintf(bitrateStr, sizeof(bitrateStr), "Bit:   %d kbps", mp3Bitrate);
            drawStr(260, 124, bitrateStr, C_TEXT_G);
        } else {
            guRect(142, 50, 100, 100, C_PANEL_MAIN);
            drawStr(260, 80, "No song playing", C_TEXT_G);
        }
    } else if (activeMenu == MENU_SETTINGS) {
        drawStr(136, 20, "Settings & Diagnostics", C_VAPOR_CYAN);

        char temp[64];
        snprintf(temp, sizeof(temp), "AVCODEC Load: 0x%08X", debugAvcodecLoadRes);
        drawStr(136, 44, temp, C_TEXT_G);

        snprintf(temp, sizeof(temp), "MP3 Load:     0x%08X", debugMp3LoadRes);
        drawStr(136, 58, temp, C_TEXT_G);

        snprintf(temp, sizeof(temp), "File Open:    0x%08X", debugFileOpenRes);
        drawStr(136, 72, temp, C_TEXT_G);

        char shortPath[24];
        strncpy(shortPath, debugLastOpenedPath, sizeof(shortPath));
        shortPath[23] = '\0';
        snprintf(temp, sizeof(temp), "Path: %s", shortPath);
        drawStr(136, 86, temp, C_TEXT_G);

        snprintf(temp, sizeof(temp), "MP3 Reserve:  0x%08X", debugMp3ReserveRes);
        drawStr(136, 100, temp, C_TEXT_G);

        snprintf(temp, sizeof(temp), "MP3 Init:     0x%08X", debugMp3InitRes);
        drawStr(136, 114, temp, C_TEXT_G);

        snprintf(temp, sizeof(temp), "Decoder Res:  0x%08X", debugDecodeRes);
        drawStr(136, 128, temp, C_TEXT_G);

        snprintf(temp, sizeof(temp), "SidebarFocus: %d  Songs: %d", sidebarFocused, songCount);
        drawStr(136, 142, temp, C_TEXT_G);
    }

    /* Bottom Player Bar (Aligned to multiples of 8: x=8, y=216, w=464, h=48) */
    drawRetroPanel(8, 216, 464, 48, C_PANEL_PLAY);

    /* Bottom Album Art icon */
    guRect(16, 222, 36, 36, C_VAPOR_PINK);
    if (playState != PLAY_STATE_STOPPED && hasCover) {
        drawTexRect(18, 224, 32, 32);
    } else if (playState != PLAY_STATE_STOPPED) {
        guRect(18, 224, 32, 32, C_VAPOR_CYAN);
    } else {
        guRect(18, 224, 32, 32, C_PANEL_MAIN);
    }

    /* Bottom player bar texts */
    if (playState != PLAY_STATE_STOPPED) {
        char bottomName[12];
        strncpy(bottomName, activeTrackName, sizeof(bottomName));
        bottomName[11] = '\0';
        char *dot = strrchr(bottomName, '.');
        if (dot) *dot = '\0';

        drawStr(60, 224, bottomName, (playState == PLAY_STATE_PLAYING) ? C_VAPOR_CYAN : C_TEXT_W);
        drawStr(60, 238, "Local File", C_TEXT_G);
    } else {
        drawStr(60, 224, "Stopped", C_TEXT_G);
    }

    /* Track Duration counters */
    int elapsedM = (int)elapsedSeconds / 60;
    int elapsedS = (int)elapsedSeconds % 60;
    int totalM = totalDuration / 60;
    int totalS = totalDuration % 60;

    char timeStr[16];
    snprintf(timeStr, sizeof(timeStr), "%d:%02d", elapsedM, elapsedS);
    drawStr(176, 224, timeStr, C_TEXT_G);

    snprintf(timeStr, sizeof(timeStr), "%d:%02d", totalM, totalS);
    drawStr(340, 224, timeStr, C_TEXT_G);

    /* Bottom Player Progress bar line */
    guRect(216, 226, 116, 4, C_PANEL_MAIN);
    if (playState != PLAY_STATE_STOPPED && totalDuration > 0) {
        int fillW = (int)(((float)elapsedSeconds / (float)totalDuration) * 116.0f);
        if (fillW > 116) fillW = 116;
        if (fillW < 0) fillW = 0;
        guRect(216, 226, fillW, 4, C_VAPOR_CYAN);
    }

    /* Volume label & bar */
    drawStr(176, 240, "VOL", C_TEXT_G);
    guRect(216, 242, 80, 3, C_PANEL_MAIN);
    int volW = (playVolume * 80) / 32768;
    guRect(216, 242, volW, 3, C_VAPOR_PINK);

    /* Bottom visualizer dynamic bars */
    for (int i = 0; i < 12; i++) {
        int h = (int)barHeights[i];
        int xPos = 380 + i * 5;
        guRect(xPos, 250 - h, 3, h, C_VAPOR_CYAN);
    }

    sceGuFinish();
    sceGuSync(0, 0);

    /* Phase 3: Vblank Sync & Buffer Swap */
    sceDisplayWaitVblankStart();
    sceGuSwapBuffers();

    void* tmp = drawBuf;
    drawBuf   = dispBuf;
    dispBuf   = tmp;
}

/* ============================================================
 *  Exit Callbacks
 * ============================================================ */
static int exitCb(int a1, int a2, void *c)
{
    (void)a1; (void)a2; (void)c;
    playState = PLAY_STATE_STOPPED;
    sceKernelExitGame();
    return 0;
}

static int cbThread(SceSize a, void *p)
{
    (void)a; (void)p;
    int id = sceKernelCreateCallback("Exit Callback", exitCb, NULL);
    sceKernelRegisterExitCallback(id);
    sceKernelSleepThreadCB();
    return 0;
}

static void setupCallbacks(void)
{
    int tid = sceKernelCreateThread("cb_thread", cbThread, 0x11, 0xFA0, 0, 0);
    if (tid >= 0) sceKernelStartThread(tid, 0, 0);
}

/* ============================================================
 *  Main
 * ============================================================ */
int main(void)
{
    setupCallbacks();
    initGU();

    /* Init custom retro font texture sheet */
    initFont();

    /* Load AV modules dynamically first (mandatory for sceMp3 to work in user mode) */
    debugAvcodecLoadRes = sceUtilityLoadModule(PSP_MODULE_AV_AVCODEC);
    debugMp3LoadRes = sceUtilityLoadModule(PSP_MODULE_AV_MP3);

    /* Init MP3 decoding module resource */
    sceMp3InitResource();

    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    /* Scan music directories */
    scanMusicDir("ms0:/MUSIC");
    scanMusicDir("ms0:/PSP/MUSIC");

    srand(12345);

    /* Wait for buttons to be released so we don't capture launch button from XMB */
    SceCtrlData padTemp;
    do {
        sceCtrlReadBufferPositive(&padTemp, 1);
        sceKernelDelayThread(10000);
    } while (padTemp.Buttons != 0);

    SceCtrlData pad, prev;
    memset(&prev, 0, sizeof(prev));

    while (1) {
        sceCtrlReadBufferPositive(&pad, 1);
        unsigned int btn  = pad.Buttons;
        unsigned int pbtn = prev.Buttons;

        /* Category menu switching (L/R triggers play previous/next track) */
        if ((btn & PSP_CTRL_LTRIGGER) && !(pbtn & PSP_CTRL_LTRIGGER)) {
            /* Prev track */
            if (songCount > 0 && activeTrackIndex >= 0) {
                int prevIdx = activeTrackIndex - 1;
                if (prevIdx < 0) prevIdx = songCount - 1;
                startSong(prevIdx);
            }
        }
        if ((btn & PSP_CTRL_RTRIGGER) && !(pbtn & PSP_CTRL_RTRIGGER)) {
            /* Next track */
            if (songCount > 0 && activeTrackIndex >= 0) {
                int nextIdx = activeTrackIndex + 1;
                if (nextIdx >= songCount) nextIdx = 0;
                startSong(nextIdx);
            }
        }

        /* Volume controls (D-Pad Left/Right now only work when L/R triggers are held to prevent conflicts, or we can just keep them active) */
        int triggerHeld = (btn & PSP_CTRL_LTRIGGER) || (btn & PSP_CTRL_RTRIGGER);
        if (triggerHeld) {
            if ((btn & PSP_CTRL_LEFT) && !(pbtn & PSP_CTRL_LEFT)) {
                playVolume -= 2048;
                if (playVolume < 0) playVolume = 0;
            }
            if ((btn & PSP_CTRL_RIGHT) && !(pbtn & PSP_CTRL_RIGHT)) {
                playVolume += 2048;
                if (playVolume > 32768) playVolume = 32768;
            }
        }

        /* D-Pad Focus Navigation */
        if (sidebarFocused) {
            /* Sidebar is focused */
            if ((btn & PSP_CTRL_UP) && !(pbtn & PSP_CTRL_UP)) {
                if (activeMenu > MENU_LOCAL_FILES) {
                    activeMenu--;
                }
            }
            if ((btn & PSP_CTRL_DOWN) && !(pbtn & PSP_CTRL_DOWN)) {
                if (activeMenu < MENU_SETTINGS) {
                    activeMenu++;
                }
            }
            /* Right focuses main content (if we are on Local Files and have tracks) */
            if ((btn & PSP_CTRL_RIGHT) && !(pbtn & PSP_CTRL_RIGHT)) {
                if (activeMenu == MENU_LOCAL_FILES && songCount > 0) {
                    sidebarFocused = 0;
                }
            }
            if ((btn & PSP_CTRL_CROSS) && !(pbtn & PSP_CTRL_CROSS)) {
                if (activeMenu == MENU_LOCAL_FILES && songCount > 0) {
                    sidebarFocused = 0;
                }
            }
        } else {
            /* Track list is focused */
            if (!triggerHeld) {
                if ((btn & PSP_CTRL_UP) && !(pbtn & PSP_CTRL_UP)) {
                    if (selectedSong > 0) {
                        selectedSong--;
                        if (selectedSong < scrollOffset) {
                            scrollOffset = selectedSong;
                        }
                    }
                }
                if ((btn & PSP_CTRL_DOWN) && !(pbtn & PSP_CTRL_DOWN)) {
                    if (selectedSong < songCount - 1) {
                        selectedSong++;
                        if (selectedSong >= scrollOffset + 9) {
                            scrollOffset = selectedSong - 8;
                        }
                    }
                }
                /* Left returns focus to the sidebar */
                if ((btn & PSP_CTRL_LEFT) && !(pbtn & PSP_CTRL_LEFT)) {
                    sidebarFocused = 1;
                }
            }

            /* Cross (X) selects and plays */
            if ((btn & PSP_CTRL_CROSS) && !(pbtn & PSP_CTRL_CROSS)) {
                startSong(selectedSong);
            }
        }

        /* Triangle (△) to Pause/Resume current song */
        if ((btn & PSP_CTRL_TRIANGLE) && !(pbtn & PSP_CTRL_TRIANGLE)) {
            if (playState == PLAY_STATE_PLAYING) {
                playState = PLAY_STATE_PAUSED;
            } else if (playState == PLAY_STATE_PAUSED) {
                playState = PLAY_STATE_PLAYING;
            }
        }

        /* Square (□) to Stop current playback */
        if ((btn & PSP_CTRL_SQUARE) && !(pbtn & PSP_CTRL_SQUARE)) {
            playState = PLAY_STATE_STOPPED;
        }

        /* Dynamic Visualizer */
        updateVisualizer();

        /* Rendering */
        render();

        prev = pad;
        sceKernelDelayThread(16666); /* target ~60 FPS update cycle */
    }

    /* Unload modules */
    sceMp3TermResource();
    sceUtilityUnloadModule(PSP_MODULE_AV_MP3);
    sceUtilityUnloadModule(PSP_MODULE_AV_AVCODEC);
    sceGuTerm();
    sceKernelExitGame();
    return 0;
}
