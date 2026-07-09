/*
 * ============================================================
 *  PSP Snake  -  pspgu-based rendering (v3)
 * ============================================================
 *  Controls
 *    D-Pad        Move snake
 *    START        Pause / Resume
 *    X (cross)    Restart  (GAME OVER)
 *    HOME         Exit to XMB
 *
 *  Rendering: pspgu (GE hardware sprites) + pspDebugScreen for
 *  text, synchronised to the correct double-buffer each frame.
 * ============================================================
 */
#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspdebug.h>
#include <pspctrl.h>
#include <psprtc.h>
#include <pspgu.h>
#include <pspge.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

PSP_MODULE_INFO("Snake", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);
PSP_HEAP_SIZE_KB(8192);

/* ============================================================
 *  Constants
 * ============================================================ */
#define SCR_W    480
#define SCR_H    272
#define VRAM_W   512                      /* framebuffer stride (pixels)     */
#define FBSIZE   (VRAM_W * SCR_H * 4)    /* 557 056 bytes per colour buffer  */

#define HEADER_H 16
#define CELL     16
#define GRID_W   (SCR_W / CELL)               /* 30 columns                 */
#define GRID_H   ((SCR_H - HEADER_H) / CELL)  /* 16 rows                   */
#define GRID_Y   HEADER_H
#define MAX_SNAKE (GRID_W * GRID_H)

/* ============================================================
 *  Colours  (PSP ABGR 32-bit: 0xAABBGGRR)
 * ============================================================ */
#define C_BG       0xFF1A1028u
#define C_GRID_A   0xFF1E152Eu
#define C_GRID_B   0xFF1A1028u
#define C_HEADER   0xFF0A0815u
#define C_BORDER   0xFF443355u
#define C_SNAKE    0xFF44CC66u
#define C_SNAKE_D  0xFF339955u
#define C_HEAD     0xFF88FFCCu
#define C_EYE      0xFF0A0815u
#define C_FOOD     0xFF3232DCu
#define C_FOOD_HL  0xFF7777EEu
#define C_WHITE    0xFFFFFFFFu
#define C_GREEN_T  0xFF44CC66u
#define C_YELLOW   0xFF00FFFFu
#define C_RED_T    0xFF3333EEu
#define C_GRAY     0xFF888888u
#define C_OVERLAY  0xFF180E28u
#define C_OVBORD   0xFF6644AAu

/* ============================================================
 *  Types
 * ============================================================ */
typedef enum { DIR_UP, DIR_DOWN, DIR_LEFT, DIR_RIGHT } Dir;
typedef enum { S_PLAY, S_PAUSE, S_DEAD }               State;
typedef struct { int x, y; }                            Pt;

/* GU 2D sprite vertex: three 16-bit signed screen coordinates             */
typedef struct { short x, y, z; } Vtx;

/* ============================================================
 *  Globals
 * ============================================================ */
/* GU display list - 256 KB, 16-byte aligned                               */
static unsigned int __attribute__((aligned(16))) dispList[65536];

/* Double-buffer VRAM offsets (NOT absolute addresses):
 *   drawBuf = the buffer currently being rendered into
 *   dispBuf = the buffer currently shown on screen                         */
static void* drawBuf = (void*)0;       /* offset 0       = first  buffer    */
static void* dispBuf = (void*)FBSIZE;  /* offset 557056  = second buffer    */

/* Game state */
static Pt    snake[MAX_SNAKE];
static int   slen;
static Dir   dir, ndir;
static Pt    food;
static int   score, best;
static State state;
static float speed;
static u64   lastTick;
static u64   tickHz;

/* ============================================================
 *  GU helper: draw a coloured rectangle via hardware sprites
 *  GU_TRANSFORM_2D maps vertex (x,y) directly to screen pixels.
 * ============================================================ */
static void guRect(int x, int y, int w, int h, u32 color)
{
    sceGuColor(color);
    Vtx* v = (Vtx*)sceGuGetMemory(2 * sizeof(Vtx));
    v[0].x = (short)x;       v[0].y = (short)y;       v[0].z = 0;
    v[1].x = (short)(x + w); v[1].y = (short)(y + h); v[1].z = 0;
    sceGuDrawArray(GU_SPRITES, GU_VERTEX_16BIT | GU_TRANSFORM_2D, 2, 0, v);
}

/* ============================================================
 *  Initialise the GU with double-buffered 32-bit colour
 * ============================================================ */
static void initGU(void)
{
    sceGuInit();
    sceGuStart(GU_DIRECT, dispList);

    /* Draw buffer, display buffer, depth buffer (VRAM offsets)             */
    sceGuDrawBuffer(GU_PSM_8888, drawBuf, VRAM_W);
    sceGuDispBuffer(SCR_W, SCR_H, dispBuf, VRAM_W);
    sceGuDepthBuffer((void*)(FBSIZE * 2), VRAM_W);  /* placed after both FB */

    /* Standard GU setup for a 2D application                               */
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
    sceGuDisable(GU_BLEND);
    sceGuDisable(GU_ALPHA_TEST);

    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);
}

/* ============================================================
 *  Game logic
 * ============================================================ */
static void spawnFood(void)
{
    Pt f; int ok;
    do {
        f.x = rand() % GRID_W;
        f.y = rand() % GRID_H;
        ok  = 1;
        for (int i = 0; i < slen; i++)
            if (snake[i].x == f.x && snake[i].y == f.y) { ok = 0; break; }
    } while (!ok);
    food = f;
}

static void initGame(void)
{
    slen  = 3;
    dir   = DIR_RIGHT;
    ndir  = DIR_RIGHT;
    score = 0;
    state = S_PLAY;
    speed = 0.15f;

    int sx = GRID_W / 2, sy = GRID_H / 2;
    for (int i = 0; i < slen; i++) { snake[i].x = sx - i; snake[i].y = sy; }

    spawnFood();
    sceRtcGetCurrentTick(&lastTick);
}

static void stepGame(void)
{
    for (int i = slen - 1; i > 0; i--) snake[i] = snake[i - 1];

    dir = ndir;
    switch (dir) {
        case DIR_UP:    snake[0].y--; break;
        case DIR_DOWN:  snake[0].y++; break;
        case DIR_LEFT:  snake[0].x--; break;
        case DIR_RIGHT: snake[0].x++; break;
    }

    /* Wall collision */
    if (snake[0].x < 0 || snake[0].x >= GRID_W ||
        snake[0].y < 0 || snake[0].y >= GRID_H)
    { if (score > best) best = score; state = S_DEAD; return; }

    /* Self collision */
    for (int i = 1; i < slen; i++)
        if (snake[0].x == snake[i].x && snake[0].y == snake[i].y)
        { if (score > best) best = score; state = S_DEAD; return; }

    /* Eat food */
    if (snake[0].x == food.x && snake[0].y == food.y) {
        score++;
        if (slen < MAX_SNAKE) slen++;
        if (score % 5 == 0 && speed > 0.05f) speed -= 0.01f;
        spawnFood();
    }
}

/* ============================================================
 *  Render one frame
 * ============================================================ */
static void render(void)
{
    /* ------------------------------------------------------------------ */
    /* Phase 1: GU hardware rendering into drawBuf                         */
    /* ------------------------------------------------------------------ */
    sceGuStart(GU_DIRECT, dispList);

    sceGuClearColor(C_BG);
    sceGuClear(GU_COLOR_BUFFER_BIT);

    /* Checker grid */
    for (int gy = 0; gy < GRID_H; gy++)
        for (int gx = 0; gx < GRID_W; gx++) {
            u32 c = ((gx + gy) & 1) ? C_GRID_A : C_GRID_B;
            guRect(gx * CELL, GRID_Y + gy * CELL, CELL, CELL, c);
        }

    /* Border line under header */
    guRect(0, GRID_Y - 1, SCR_W, 1, C_BORDER);

    /* Food – pulses between two sizes at ~4 Hz */
    u64 t;
    sceRtcGetCurrentTick(&t);
    int pulse = (int)((t * 4ULL) / tickHz) & 1;  /* avoids division by zero */
    int fx = food.x * CELL, fy = GRID_Y + food.y * CELL;
    int fp = pulse ? 3 : 4;
    guRect(fx + fp, fy + fp, CELL - fp * 2, CELL - fp * 2, C_FOOD);
    if (!pulse) guRect(fx + 6, fy + 6, CELL - 12, CELL - 12, C_FOOD_HL);

    /* Snake segments */
    for (int i = slen - 1; i >= 0; i--) {
        int sx = snake[i].x * CELL;
        int sy = GRID_Y + snake[i].y * CELL;

        if (i == 0) {
            /* Head */
            guRect(sx + 1, sy + 1, CELL - 2, CELL - 2, C_HEAD);
            /* Eyes – shift with direction */
            switch (dir) {
                case DIR_RIGHT:
                    guRect(sx+CELL-5, sy+3,      2,2, C_EYE);
                    guRect(sx+CELL-5, sy+CELL-5, 2,2, C_EYE); break;
                case DIR_LEFT:
                    guRect(sx+3, sy+3,      2,2, C_EYE);
                    guRect(sx+3, sy+CELL-5, 2,2, C_EYE); break;
                case DIR_UP:
                    guRect(sx+3,      sy+3, 2,2, C_EYE);
                    guRect(sx+CELL-5, sy+3, 2,2, C_EYE); break;
                case DIR_DOWN:
                    guRect(sx+3,      sy+CELL-5, 2,2, C_EYE);
                    guRect(sx+CELL-5, sy+CELL-5, 2,2, C_EYE); break;
            }
        } else {
            /* Body – taper toward the tail */
            u32 c = (i < slen / 2) ? C_SNAKE : C_SNAKE_D;
            int p = (i > (slen * 2 / 3)) ? 3 : 2;
            guRect(sx + p, sy + p, CELL - p * 2, CELL - p * 2, c);
        }
    }

    /* Header bar */
    guRect(0, 0, SCR_W, HEADER_H, C_HEADER);

    /* Overlay boxes (drawn before text so text appears on top) */
    if (state == S_DEAD) {
        guRect(110, 92,  260, 90, C_OVBORD);
        guRect(112, 94,  256, 86, C_OVERLAY);
    }
    if (state == S_PAUSE) {
        guRect(155, 102, 170, 68, C_OVBORD);
        guRect(157, 104, 166, 64, C_OVERLAY);
    }

    sceGuFinish();
    sceGuSync(0, 0);

    /* ------------------------------------------------------------------ */
    /* Phase 2: Text via pspDebugScreen, targeting the same drawBuf        */
    /*                                                                      */
    /* pspDebugScreenSetOffset() sets vram_base = 0x44000000 + offset.    */
    /* drawBuf is a VRAM byte-offset (0 or FBSIZE), matching this scheme.  */
    /* ------------------------------------------------------------------ */
    pspDebugScreenSetOffset((int)drawBuf);

    /* Header text */
    pspDebugScreenSetBackColor(C_HEADER);
    pspDebugScreenSetXY(0, 0);
    pspDebugScreenSetTextColor(C_GREEN_T);
    pspDebugScreenPrintf("SNAKE");

    pspDebugScreenSetXY(7, 0);
    pspDebugScreenSetTextColor(C_WHITE);
    pspDebugScreenPrintf("Score:%4d", score);

    pspDebugScreenSetXY(19, 0);
    pspDebugScreenSetTextColor(C_YELLOW);
    pspDebugScreenPrintf("Best:%4d", best);

    pspDebugScreenSetXY(30, 0);
    pspDebugScreenSetTextColor(C_GRAY);
    pspDebugScreenPrintf("D-Pad=move  START=pause");

    /* Overlay text */
    if (state == S_DEAD) {
        pspDebugScreenSetBackColor(C_OVERLAY);
        pspDebugScreenSetXY(15, 12);
        pspDebugScreenSetTextColor(C_RED_T);
        pspDebugScreenPrintf("  GAME  OVER  ");

        pspDebugScreenSetXY(11, 13);
        pspDebugScreenSetTextColor(C_WHITE);
        pspDebugScreenPrintf("Score:%d  Best:%d", score, best);

        pspDebugScreenSetXY(11, 14);
        pspDebugScreenSetTextColor(0xFF66FF88u);
        pspDebugScreenPrintf("Press X to play again");

        pspDebugScreenSetXY(14, 15);
        pspDebugScreenSetTextColor(C_GRAY);
        pspDebugScreenPrintf("HOME to exit");
    }
    if (state == S_PAUSE) {
        pspDebugScreenSetBackColor(C_OVERLAY);
        pspDebugScreenSetXY(21, 13);
        pspDebugScreenSetTextColor(C_YELLOW);
        pspDebugScreenPrintf("PAUSED");

        pspDebugScreenSetXY(16, 15);
        pspDebugScreenSetTextColor(C_WHITE);
        pspDebugScreenPrintf("START to resume");
    }

    /* ------------------------------------------------------------------ */
    /* Phase 3: Vsync + swap                                               */
    /* After sceGuSwapBuffers() the GU automatically uses the old dispBuf  */
    /* as the new draw target, matching our manual tracking below.         */
    /* ------------------------------------------------------------------ */
    sceDisplayWaitVblankStart();
    sceGuSwapBuffers();

    /* Manually track the swap so pspDebugScreenSetOffset is correct next  */
    void* tmp = drawBuf;
    drawBuf   = dispBuf;
    dispBuf   = tmp;
}

/* ============================================================
 *  Exit callback  (HOME → XMB)
 * ============================================================ */
static int exitCb(int a1, int a2, void *c)
{
    (void)a1; (void)a2; (void)c;
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

    /* GU handles all display setup (mode, framebuffer, vsync)             */
    initGU();

    /*
     * Init pspDebugScreen WITHOUT touching the display (setup=0).
     * sceGeEdramGetAddr() returns 0x04000000 – the VRAM base.
     * Combined with pspDebugScreenSetOffset(), text is routed to
     * whichever draw buffer the GU is currently targeting.
     */
    pspDebugScreenInitEx(sceGeEdramGetAddr(),
                         PSP_DISPLAY_PIXEL_FORMAT_8888,
                         0 /* don't re-init display */);

    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    tickHz = sceRtcGetTickResolution();
    if (!tickHz) tickHz = 1000000ULL;   /* safety fallback */

    sceRtcGetCurrentTick(&lastTick);
    srand((unsigned int)(lastTick & 0xFFFFFFFF));

    initGame();

    SceCtrlData pad, prev;
    memset(&prev, 0, sizeof(prev));

    while (1) {
        sceCtrlReadBufferPositive(&pad, 1);
        unsigned int btn  = pad.Buttons;
        unsigned int pbtn = prev.Buttons;

        /* Input */
        if (state == S_PLAY) {
            if ((btn & PSP_CTRL_UP)    && dir != DIR_DOWN)  ndir = DIR_UP;
            if ((btn & PSP_CTRL_DOWN)  && dir != DIR_UP)    ndir = DIR_DOWN;
            if ((btn & PSP_CTRL_LEFT)  && dir != DIR_RIGHT) ndir = DIR_LEFT;
            if ((btn & PSP_CTRL_RIGHT) && dir != DIR_LEFT)  ndir = DIR_RIGHT;
            if ((btn & PSP_CTRL_START) && !(pbtn & PSP_CTRL_START))
                state = S_PAUSE;
        } else if (state == S_PAUSE) {
            if ((btn & PSP_CTRL_START) && !(pbtn & PSP_CTRL_START))
                state = S_PLAY;
        } else if (state == S_DEAD) {
            if ((btn & PSP_CTRL_CROSS) && !(pbtn & PSP_CTRL_CROSS))
                initGame();
        }

        /* Time-driven game step */
        if (state == S_PLAY) {
            u64 now;
            sceRtcGetCurrentTick(&now);
            float dt = (float)(now - lastTick) / (float)tickHz;
            if (dt >= speed) { lastTick = now; stepGame(); }
        }

        render();
        prev = pad;
    }

    sceGuTerm();
    sceKernelExitGame();
    return 0;
}
