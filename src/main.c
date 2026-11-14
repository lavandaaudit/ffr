/*
 * main.c — IBONARIUM: KYIV — THE LAST SIGNAL
 * MISSION 01 «ОБОЛОНЬ: ПЕРШИЙ ДЕНЬ»
 *
 * Entry point: init VDP/PSG/sprite engine, title screen, game states.
 *
 * Controls (classic layout):
 *   D-pad — move; B — attack; A — interact/search;
 *   C — tap: inventory / hold: run; START — pause / confirm
 */
#include "game.h"
#include "resources.h"

/* edge-detected buttons (set each frame in main loop) */
u16 joyPressedMask = 0;
static u16 prevJoy = 0;

/* title */
static u16 titleCursor = 0;
static bool titleHasSave = FALSE;
static bool titleDrawn = FALSE;

/* C tap detection for inventory */
static u16 cHoldFrames = 0;
static bool cWasDown = FALSE;

/* ---------------------------------------------------------------- */
static void loadPalettes(void)
{
    PAL_setPaletteColors(0, &pal_hud, CPU);           /* fonts, HUD, item icons */
    PAL_setPaletteColors(16, &pal_world, CPU);        /* isometric map */
    PAL_setPaletteColors(PAL2 * 16, spr_marco.palette, CPU);  /* Marco */
    PAL_setPaletteColors(PAL3 * 16, spr_walker.palette, CPU); /* infected */
}

static void drawTitle(void)
{
    UI_clearRect(0, 0, 40, 28);
    VDP_clearPlane(BG_B, TRUE);

    UI_text(FC_BLUE, "I B O N A R I U M", 10, 6);
    UI_text(FC_WHITE, "КИЇВ — ОСТАННІЙ СИГНАЛ", 9, 9);
    UI_text(FC_CYAN, "DEMO: МІСІЯ 01 «ОБОЛОНЬ»", 8, 11);

    UI_text(FC_WHITE, "НОВА ГРА", 15, 15);
    if (titleHasSave)
        UI_text(FC_WHITE, "ПРОДОВЖИТИ", 15, 17);
    UI_text(FC_CYAN, ">", 13, 15 + titleCursor * 2);

    UI_text(FC_CYAN, "B АТАКА  A ШУКАТИ  C ІНВЕНТАР/БІГ", 3, 22);
    UI_text(FC_CYAN, "START ПАУЗА  ХОЛД C — БІГ", 3, 23);
    UI_text(FC_WHITE, "© 2029 IBONARIUM. SGDK. PLACEHOLDER ART.", 1, 27);
}

static void titleUpdate(void)
{
    if (!titleDrawn)
    {
        drawTitle();
        titleDrawn = TRUE;
    }

    if (joyPressedMask & BUTTON_UP)
    {
        if (titleCursor > 0)
        {
            titleCursor--;
            UI_text(FC_CYAN, ">", 13, 15 + titleCursor * 2);
            UI_clearRect(13, 17, 1, 1);
            SFX_play(SFX_MENU);
        }
    }
    if (joyPressedMask & BUTTON_DOWN)
    {
        u8 max = titleHasSave ? 1 : 0;
        if (titleCursor < max)
        {
            titleCursor++;
            UI_text(FC_CYAN, ">", 13, 15 + titleCursor * 2);
            UI_clearRect(13, 15, 1, 1);
            SFX_play(SFX_MENU);
        }
    }
    if ((joyPressedMask & BUTTON_START) || (joyPressedMask & BUTTON_A))
    {
        SFX_stopAll();
        if (titleCursor == 1 && titleHasSave)
        {
            M01_loadFromSave();
        }
        else
        {
            M01_initNewGame();
        }
    }
}

static void gameoverScreen(void)
{
    static bool drawn = FALSE;
    static u16 timer = 0;
    if (!drawn)
    {
        UI_clearRect(0, 0, 40, 28);
        VDP_clearPlane(BG_B, TRUE);
        UI_text(FC_RED, "ВИ ЗАГИНУЛИ", 14, 12);
        UI_text(FC_WHITE, "СИГНАЛ НЕ ВІДПРАВЛЕНО", 9, 15);
        drawn = TRUE;
        timer = 0;
        SFX_stopAll();
    }
    timer++;
    if (timer > 180 || (joyPressedMask & BUTTON_START))
    {
        drawn = FALSE;
        gameState = GS_TITLE;
        titleHasSave = SAVE_exists();
        titleCursor = 0;
        titleDrawn = FALSE;
        /* release sprites */
        SPR_reset();
        SPR_init();
    }
}

/* ---------------------------------------------------------------- C tap */
static void inventoryInput(void)
{
    bool cDown = (JOY_readJoypad(JOY_1) & BUTTON_C) != 0;
    if (cDown && !cWasDown)
        cHoldFrames = 0;
    else if (cDown && cWasDown)
        cHoldFrames++;
    else if (!cDown && cWasDown)
    {
        /* released: quick tap (not used for run) -> inventory */
        if (cHoldFrames < 12 && gameState == GS_PLAY)
        {
            gameState = GS_INVENTORY;
        }
    }
    cWasDown = cDown;
}

/* ---------------------------------------------------------------- */
int main(bool hardReset)
{
    SYS_disableInts();
    VDP_setScreenWidth320();
    VDP_setScreenHeight224();
    SYS_enableInts();

    JOY_init();
    SPR_init();

    UI_init();
    loadPalettes();
    SFX_init();
    CBT_init();
    HUD_init();

    titleHasSave = SAVE_exists();
    titleCursor = 0;
    titleDrawn = FALSE;
    gameState = GS_TITLE;

    while (TRUE)
    {
        u16 joy = JOY_readJoypad(JOY_1);
        joyPressedMask = joy & ~prevJoy;
        prevJoy = joy;

        switch (gameState)
        {
        case GS_TITLE:
            titleUpdate();
            break;

        case GS_INTRO:
            M01_introUpdate();
            SFX_update();
            break;

        case GS_PLAY:
            inventoryInput();
            if (joyPressedMask & BUTTON_B)
                CBT_playerAttack(&player);
            if (joyPressedMask & BUTTON_A)
                M01_tryInteract();
            if (joyPressedMask & BUTTON_START)
            {
                gameState = GS_PAUSE;
                HUD_setPrompt(NULL);
            }
            M01_update();
            break;

        case GS_PAUSE:
            UI_pauseScreen();
            SFX_update();
            break;

        case GS_INVENTORY:
            UI_inventoryScreen();
            SFX_update();
            break;

        case GS_CUTSCENE:
            M01_cutsceneUpdate();
            break;

        case GS_END:
            if (joyPressedMask & BUTTON_START)
            {
                gameState = GS_TITLE;
                titleHasSave = SAVE_exists();
                titleCursor = 0;
                titleDrawn = FALSE;
                SPR_reset();
                SPR_init();
            }
            break;

        case GS_GAMEOVER:
            gameoverScreen();
            break;
        }

        SPR_update();
        SYS_doVBlankProcess();
    }
    return 0;
}
