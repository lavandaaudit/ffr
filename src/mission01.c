/*
 * mission01.c — ОБОЛОНЬ: ПЕРШИЙ ДЕНЬ.
 * Tasks, interactions with world objects, pickups, zone triggers,
 * intro / radio-ending cutscenes, pause & inventory screens.
 */
#include "game.h"
#include "resources.h"

GameState gameState = GS_TITLE;
Player player;
Inventory inventory;
SaveData saveData;
u32 gameFlags = 0;
u8 currentTask = TASK_NONE;
bool missionComplete = FALSE;
u16 frameCount = 0;

/* pickup sprites */
static Sprite *pickupSprites[WORLD_PICKUP_COUNT];
static bool pickupActive[WORLD_PICKUP_COUNT];

/* ---------------------------------------------------------------- flags */
bool M01_flagPickup(u16 idx)      { return (gameFlags >> (GAMEFLAG_PICKUP_BASE + idx)) & 1; }
void M01_setFlagPickup(u16 idx)   { gameFlags |= 1L << (GAMEFLAG_PICKUP_BASE + idx); }
bool M01_flagObject(u16 idx)      { return (gameFlags >> (GAMEFLAG_OBJECT_BASE + idx)) & 1; }
void M01_setFlagObject(u16 idx)   { gameFlags |= 1L << (GAMEFLAG_OBJECT_BASE + idx); }

/* ---------------------------------------------------------------- tasks */
static const char *taskTexts[MAX_TASK + 1] = {
    NULL,
    "ЗНАЙДІТЬ ВОДУ",
    "ЗНАЙДІТЬ КУХОННИЙ НІЖ",
    "ЗНАЙДІТЬ ВИХІД ІЗ ПІД'ЇЗДУ",
    "НЕБЕЗПЕКА ПОБЛИЗУ",
    "SIGNAL DETECTED"
};

void setTask(u8 task)
{
    if (task <= currentTask && task != TASK_NONE) return;
    currentTask = task;
    HUD_setTask(taskTexts[task]);
    HUD_message("ЗАВДАННЯ ОНОВЛЕНО", taskTexts[task]);
    SFX_play(SFX_MENU);
}

/* ---------------------------------------------------------------- pickups */
static void spawnPickupSprites(void)
{
    u16 i;
    for (i = 0; i < WORLD_PICKUP_COUNT; i++)
    {
        const ItemSpawnDef *ps = &world_pickups[i];
        pickupActive[i] = !M01_flagPickup(i);
        if (pickupActive[i])
        {
            s16 sx = worldToScreenX(ps->u << 8, ps->v << 8);
            s16 sy = worldToScreenY(ps->u << 8, ps->v << 8);
            pickupSprites[i] = SPR_addSprite(&spr_items,
                                             sx - camera.x - 8,
                                             sy - camera.y - 14,
                                             TILE_ATTR(PAL2, FALSE, FALSE, FALSE));
            SPR_setFrame(pickupSprites[i], ps->item);
            SPR_setZ(pickupSprites[i], sy);
        }
        else
            pickupSprites[i] = NULL;
    }
}

static void removePickupSprite(u16 idx)
{
    if (pickupSprites[idx])
    {
        SPR_releaseSprite(pickupSprites[idx]);
        pickupSprites[idx] = NULL;
    }
    pickupActive[idx] = FALSE;
    M01_setFlagPickup(idx);
}

static void updatePickupSprites(void)
{
    u16 i;
    for (i = 0; i < WORLD_PICKUP_COUNT; i++)
    {
        if (pickupActive[i] && pickupSprites[i])
        {
            const ItemSpawnDef *ps = &world_pickups[i];
            s16 sx = worldToScreenX(ps->u << 8, ps->v << 8);
            s16 sy = worldToScreenY(ps->u << 8, ps->v << 8);
            SPR_setPosition(pickupSprites[i], sx - camera.x - 8, sy - camera.y - 14);
            SPR_setZ(pickupSprites[i], sy - 1);
        }
    }
}

bool M01_checkPickupAt(s16 u_q8, s16 v_q8, s16 range_q8, s16 *idx)
{
    u16 i;
    for (i = 0; i < WORLD_PICKUP_COUNT; i++)
    {
        if (!pickupActive[i]) continue;
        {
            const ItemSpawnDef *ps = &world_pickups[i];
            s32 du = (s32)(ps->u << 8) - u_q8;
            s32 dv = (s32)(ps->v << 8) - v_q8;
            s32 d = abs(du) + abs(dv);
            if (d <= range_q8)
            {
                *idx = (s16)i;
                return TRUE;
            }
        }
    }
    return FALSE;
}

bool M01_checkObjectAt(s16 u_q8, s16 v_q8, s16 range_q8, s16 *idx)
{
    u16 i;
    for (i = 0; i < WORLD_OBJECT_COUNT; i++)
    {
        const ObjectDef *od = &world_objects[i];
        s32 du = (s32)(od->u << 8) - u_q8;
        s32 dv = (s32)(od->v << 8) - v_q8;
        s32 d = abs(du) + abs(dv);
        if (d <= range_q8)
        {
            *idx = (s16)i;
            return TRUE;
        }
    }
    return FALSE;
}

/* ---------------------------------------------------------------- interactions */
static bool giveItem(ItemType it, u8 count)
{
    s8 r = INV_add(&inventory, it, count);
    if (r < 0)
    {
        HUD_message("ІНВЕНТАР ПОВНИЙ!", NULL);
        return FALSE;
    }
    SFX_play(SFX_PICKUP);
    HUD_message("ОТРИМАНО:", itemDefs[it].name);
    return TRUE;
}

static void completeTaskWaterKnife(void)
{
    /* advance task chain: water -> knife -> exit */
    if (currentTask == TASK_WATER && INV_count(&inventory, I_WATER) > 0)
        setTask(TASK_KNIFE);
    if (currentTask == TASK_KNIFE && INV_count(&inventory, I_KNIFE) > 0)
        setTask(TASK_EXIT);
}

void M01_tryInteract(void)
{
    s16 idx;
    if (player.dead) return;

    /* pickups first */
    if (M01_checkPickupAt(player.u_q8, player.v_q8, 384, &idx))
    {
        const ItemSpawnDef *ps = &world_pickups[idx];
        PLR_setAnim(&player, AN_M_PICKUP, TRUE);
        player.animTimer = 0;
        if (giveItem((ItemType)ps->item, 1))
        {
            removePickupSprite(idx);
            completeTaskWaterKnife();
        }
        return;
    }

    if (M01_checkObjectAt(player.u_q8, player.v_q8, 640, &idx))
    {
        const ObjectDef *od = &world_objects[idx];
        PLR_setAnim(&player, AN_M_USE, TRUE);
        SFX_play(SFX_USE);

        switch (od->type)
        {
        case OBJ_PHONE:
            HUD_message("ТЕЛЕФОН: НІ СИГНАЛУ.", "ЛІНІЯ МЕРТВА.");
            break;
        case OBJ_TV:
            HUD_message("ТЕЛЕВІЗОР: ЕКРАН БИЙ.", "ТИЛЬКИ ШИПІННЯ.");
            break;
        case OBJ_RADIO_APT:
            SFX_setAmbient(AMB_RADIO);
            HUD_message("РАДІО: ЛИШЕ ШИПІННЯ...", "ДИВНИЙ СИГНАЛ БУДЬ-ЯК СЬОГОДНІ.");
            break;
        case OBJ_FRIDGE:
            if (!M01_flagObject(idx))
            {
                M01_setFlagObject(idx);
                if (giveItem(I_WATER, 2))
                    completeTaskWaterKnife();
            }
            else
                HUD_message("ХОЛОДИЛЬНИК ПУСТИЙ.", NULL);
            break;
        case OBJ_KITCHEN:
            if (!M01_flagObject(idx))
            {
                M01_setFlagObject(idx);
                if (giveItem(I_KNIFE, 1))
                    completeTaskWaterKnife();
            }
            else
                HUD_message("СТІЛ ВЖЕ ОБШУКАЛИ.", NULL);
            break;
        case OBJ_WARDROBE:
            if (!M01_flagObject(idx))
            {
                M01_setFlagObject(idx);
                giveItem(I_BANDAGE, 1);
            }
            else
                HUD_message("В ШАФІ НІЧОГО БІЛЬШЕ.", NULL);
            break;
        case OBJ_TABLE:
            HUD_message("ПИЛЯВА ПИЛУ. КОЛІШНЄ ЖИТТЯ.", NULL);
            break;
        case OBJ_SOFA:
            HUD_message("ДИВАН. ВІДПОЧИНОК НЕ ДАЛЕ.", NULL);
            break;
        case OBJ_CAR_VAZ:
            HUD_message("ВАЗ-2101: ЗАЧИНЕНО.", "ВСЕРЕДИНІ НІЧОГО.");
            break;
        case OBJ_CAR_LANOS:
            if (!M01_flagObject(idx))
            {
                M01_setFlagObject(idx);
                if (giveItem(I_WATER, 1))
                    completeTaskWaterKnife();
            }
            else
                HUD_message("БАГАЖНИК ВЖЕ ОБШУКАЛИ.", NULL);
            break;
        case OBJ_CAR_SKODA:
            HUD_message("SKODA: СКЛО РОЗБИТЕ.", "ПУСТО.");
            break;
        case OBJ_CAR_VAN:
            if (!M01_flagObject(idx))
            {
                M01_setFlagObject(idx);
                giveItem(I_BATTERY, 1);
            }
            else
                HUD_message("ВІД МІКРОАВТОБУСА ЛИШЕ ДЕТАЛІ...", NULL);
            break;
        case OBJ_BIN_1:
        case OBJ_BIN_3:
        case OBJ_BIN_4:
            HUD_message("ЛИШЕ СМІТТЯ.", NULL);
            break;
        case OBJ_BIN_2:
            if (!M01_flagObject(idx))
            {
                M01_setFlagObject(idx);
                giveItem(I_PARTS, 2);
            }
            else
                HUD_message("ЯЩИК ПУСТИЙ.", NULL);
            break;
        case OBJ_SWING:
            HUD_message("ГОЙДАЛКА СКРИПИТЬ НА ВІТРІ.", NULL);
            break;
        case OBJ_LAMP:
            HUD_message("ФОНАР МИГАЄ.", "СТРУМ Є? ДИВНО...");
            break;
        case OBJ_RADIO_END:
            if (!missionComplete)
                M01_startEnding();
            break;
        default:
            HUD_message("НІЧОГО ОСОБЛИВОГО.", NULL);
            break;
        }
        return;
    }

    HUD_message("ПОБЛИЗУ НЕМА ЧОГО ДОТИКАТИСЯ.", NULL);
}

/* ---------------------------------------------------------------- zone logic */
static u8 lastZone = 0;

#define Z_NONE 0
#define Z_APT  1
#define Z_STAIR 2
#define Z_YARD 3
#define Z_PLAZA 4

static u8 zoneOf(Player *p)
{
    s16 u = p->u_q8 >> 8;
    s16 v = p->v_q8 >> 8;
    if (u >= ZONE_APT_U0 && u <= ZONE_APT_U1 && v >= ZONE_APT_V0 && v <= ZONE_APT_V1)
        return Z_APT;
    if (u >= ZONE_STAIR_U0 && u <= ZONE_STAIR_U1 && v >= ZONE_STAIR_V0 && v <= ZONE_STAIR_V1)
        return Z_STAIR;
    if (u >= ZONE_PLAZA_U0 && u <= ZONE_PLAZA_U1 && v >= ZONE_PLAZA_V0 && v <= ZONE_PLAZA_V1)
        return Z_PLAZA;
    return Z_YARD;
}

static void zoneUpdate(void)
{
    u8 z = zoneOf(&player);
    if (z == lastZone) return;

    if (z == Z_STAIR)
    {
        SFX_setAmbient(AMB_WIND);
    }
    else if (z == Z_YARD)
    {
        SFX_setAmbient(AMB_WIND);
        if (lastZone == Z_STAIR)
        {
            /* completed exit task */
            if (currentTask <= TASK_EXIT)
                setTask(TASK_DANGER);
            HUD_message("ДВЕРІ ВІДКРИТО. ОБОЛОНЬ ЗОВНІ.", "ЗАРАЖЕНІ ПОБЛИЗУ. БАГАТО.");
        }
    }
    else if (z == Z_PLAZA)
    {
        if (currentTask < TASK_DANGER)
            setTask(TASK_DANGER);
        HUD_message("ВЕЛИКА ФІГУРА СТОЇТЬ БІЛЯ РАДІОПРИЙМАЧА...", "ЦЕ НЕ ЗВИЧАЙНИЙ ЗАРАЖЕНИЙ. BRUTE.");
    }
    lastZone = z;
}

/* ---------------------------------------------------------------- danger alert */
static u16 dangerWarnTimer = 0;

static void dangerUpdate(void)
{
    if (currentTask >= TASK_DANGER) return;
    dangerWarnTimer++;
    if (dangerWarnTimer > 30)
    {
        dangerWarnTimer = 0;
        if (ENY_anyAggroNear(player.u_q8, player.v_q8, 8 << 8))
            setTask(TASK_DANGER);
    }
}

/* ---------------------------------------------------------------- ending */
#define CUT_MAX 7
static u8 cutStep = 0;
static u16 cutTimer = 0;

static const char *cutLines[CUT_MAX][2] = {
    { "SIGNAL DETECTED", NULL },
    { "...ЯКЩО ХТОСЬ МЕНЕ ЧУЄ...", NULL },
    { "...ПОВТОРЮЮ...", NULL },
    { "...ЦЕ НЕ ВИПАДКОВІСТЬ...", NULL },
    { "IBONARIUM...", NULL },
    { "МАРКО: ЩО ТАКЕ IBONARIUM?..", NULL },
    { NULL, NULL }
};

static void cutDrawCurrent(void)
{
    UI_clearRect(1, 20, 38, 3);
    if (cutLines[cutStep][0])
        UI_text(FC_CYAN, cutLines[cutStep][0], 2, 20);
    if (cutLines[cutStep][1])
        UI_text(FC_CYAN, cutLines[cutStep][1], 2, 21);
}

static void drawEndScreen(void)
{
    UI_clearRect(0, 0, 40, 28);
    UI_text(FC_BLUE, "MISSION COMPLETED", 11, 9);
    UI_text(FC_WHITE, "ОБОЛОНЬ: ПЕРШИЙ ДЕНЬ", 9, 12);
    UI_text(FC_CYAN, "THE SIGNAL HAS FOUND YOU", 7, 15);
    UI_text(FC_WHITE, "ГРУ ЗБЕРЕЖЕНО (SRAM)", 10, 19);
    UI_text(FC_CYAN, "START — ТИТУЛИ", 13, 24);
}

void M01_startEnding(void)
{
    gameState = GS_CUTSCENE;
    cutStep = 0;
    cutTimer = 0;
    HUD_setPrompt(NULL);
    HUD_message("SIGNAL DETECTED", NULL);
    SFX_setAmbient(AMB_RADIO);
    SFX_signalBeep(70);
}

void M01_cutsceneUpdate(void)
{
    cutTimer++;
    if (cutTimer == 1)
        cutDrawCurrent();

    /* radio beeps */
    if ((cutTimer & 31) == 16)
        SFX_signalBeep((cutStep & 1) ? 90 : 120);
    if ((cutTimer & 31) == 26)
        SFX_signalSilence();

    if (cutTimer > 200)
    {
        cutTimer = 0;
        cutStep++;
        if (cutStep >= CUT_MAX)
        {
            /* mission complete */
            missionComplete = TRUE;
            gameState = GS_END;
            SFX_stopAll();
            drawEndScreen();
            /* auto-save */
            M01_buildSaveData(&saveData);
            saveData.missionDone = 1;
            SAVE_write(&saveData);
            return;
        }
        cutDrawCurrent();
    }

    if (joyPressedMask & BUTTON_START || joyPressedMask & BUTTON_A)
    {
        /* skip forward */
        cutTimer = 199;
    }
}

/* ---------------------------------------------------------------- intro */
static u8 introStep = 0;
static u16 introTimer = 0;

static void drawIntro(void)
{
    UI_clearRect(0, 0, 40, 28);
    if (introStep == 0)
    {
        UI_text(FC_WHITE, "КИЇВ. ОБОЛОНЬ.", 13, 12);
        UI_text(FC_BLUE, "07:42", 18, 14);
    }
    else
    {
        UI_text(FC_WHITE, "МАРКО ПРОСИНАЄТЬСЯ НА ПІДЛОЗІ.", 4, 11);
        UI_text(FC_CYAN, "ЩО, ЧОРТ ЗАБИРАЙ, СТАЛОСЯ?..", 5, 13);
    }
}

void M01_introUpdate(void)
{
    introTimer++;
    if (introTimer == 1)
        drawIntro();
    if (introTimer > 210 || joyPressedMask & BUTTON_START ||
        joyPressedMask & BUTTON_A)
    {
        introStep++;
        introTimer = 0;
        if (introStep >= 2)
        {
            gameState = GS_PLAY;
            UI_clearRect(0, 0, 40, 28);
            HUD_message("ВИ РАПТОМ ПРОСИНАЄТЬСЯ.", "СПАГА. СПАГА. ДИВНИЙ ШУМ НАВКОЛО.");
        }
        else
            drawIntro();
    }
}

/* ---------------------------------------------------------------- new game / save */
void M01_initNewGame(void)
{
    INV_init(&inventory);
    gameFlags = 0;
    currentTask = TASK_NONE;
    missionComplete = FALSE;
    lastZone = Z_NONE;
    dangerWarnTimer = 0;

    SPR_reset();
    SPR_init();
    WORLD_load();
    CAM_snap();

    PLR_init(&player, PLAYER_START_U, PLAYER_START_V);
    CAM_snap();
    ENY_spawnAll();
    spawnPickupSprites();

    HUD_init();
    HUD_clearAll();
    setTask(TASK_WATER);

    gameState = GS_INTRO;
    introStep = 0;
    introTimer = 0;
    SFX_stopAll();
    SFX_setAmbient(AMB_NONE);
}

void M01_buildSaveData(SaveData *d)
{
    u16 i;
    memset(d, 0, sizeof(SaveData));
    d->magic = 0;
    d->version = 1;
    d->u_q8 = player.u_q8;
    d->v_q8 = player.v_q8;
    d->hp = player.hp;
    d->sta = player.sta;
    d->hunger = player.hunger;
    d->thirst = player.thirst;
    d->injury = player.injury;
    for (i = 0; i < INV_SLOTS; i++)
    {
        d->invItem[i] = inventory.item[i];
        d->invCount[i] = inventory.count[i];
    }
    d->weapon = inventory.weapon;
    d->taskIdx = currentTask;
    d->flags = gameFlags;
    d->missionDone = missionComplete ? 1 : 0;
    d->checksum = 0;
}

void M01_loadFromSave(void)
{
    u16 i;
    SaveData d;
    if (!SAVE_load(&d))
        return;

    SPR_reset();
    SPR_init();
    WORLD_load();

    INV_init(&inventory);
    for (i = 0; i < INV_SLOTS; i++)
    {
        inventory.item[i] = d.invItem[i];
        inventory.count[i] = d.invCount[i];
    }
    inventory.weapon = d.weapon;

    gameFlags = d.flags;
    currentTask = d.taskIdx;
    missionComplete = d.missionDone != 0;
    lastZone = Z_NONE;
    dangerWarnTimer = 0;

    PLR_init(&player, d.u_q8 >> 8, d.v_q8 >> 8);
    player.hp = d.hp;
    player.sta = d.sta;
    player.hunger = d.hunger;
    player.thirst = d.thirst;
    player.injury = d.injury;

    CAM_snap();
    ENY_spawnAll();
    spawnPickupSprites();

    HUD_init();
    HUD_clearAll();
    if (currentTask != TASK_NONE)
        HUD_setTask(taskTexts[currentTask]);
    else
        setTask(TASK_WATER);

    gameState = GS_PLAY;
    SFX_stopAll();
    SFX_setAmbient(AMB_WIND);
}

/* ---------------------------------------------------------------- pause */
static s8 pauseCursor = 0;
static const char *pauseItems[3] = { "ПРОДОВЖИТИ", "ЗБЕРЕГТИ", "У ТИТУЛИ" };

void UI_pauseScreen(void)
{
    static bool drawn = FALSE;
    if (!drawn)
    {
        UI_box(11, 9, 18, 9);
        UI_text(FC_BLUE, "ПАУЗА", 17, 10);
        UI_text(FC_WHITE, pauseItems[0], 13, 12);
        UI_text(FC_WHITE, pauseItems[1], 13, 14);
        UI_text(FC_WHITE, pauseItems[2], 13, 16);
        UI_text(FC_CYAN, ">", 12, 12 + pauseCursor * 2);
        drawn = TRUE;
    }

    if (joyPressedMask & BUTTON_UP)
    {
        pauseCursor--;
        if (pauseCursor < 0) pauseCursor = 2;
        SFX_play(SFX_MENU);
        UI_text(FC_CYAN, ">", 12, 12 + pauseCursor * 2);
        UI_clearRect(12, 12 + ((pauseCursor + 1) % 3) * 2, 1, 1);
    }
    if (joyPressedMask & BUTTON_DOWN)
    {
        pauseCursor++;
        if (pauseCursor > 2) pauseCursor = 0;
        SFX_play(SFX_MENU);
        UI_text(FC_CYAN, ">", 12, 12 + pauseCursor * 2);
        UI_clearRect(12, 12 + ((pauseCursor + 2) % 3) * 2, 1, 1);
    }
    if (joyPressedMask & BUTTON_START || joyPressedMask & BUTTON_B)
    {
        /* resume */
        UI_clearRect(11, 9, 18, 9);
        gameState = GS_PLAY;
        drawn = FALSE;
    }
    else if (joyPressedMask & BUTTON_A)
    {
        if (pauseCursor == 0)
        {
            UI_clearRect(11, 9, 18, 9);
            gameState = GS_PLAY;
            drawn = FALSE;
        }
        else if (pauseCursor == 1)
        {
            M01_buildSaveData(&saveData);
            SAVE_write(&saveData);
            SFX_play(SFX_SAVE);
            HUD_message("ГРУ ЗБЕРЕЖЕНО (SRAM).", NULL);
        }
        else
        {
            gameState = GS_TITLE;
            drawn = FALSE;
        }
    }
}

/* ---------------------------------------------------------------- inventory */
static s8 invCursor = 0;

void UI_inventoryScreen(void)
{
    static bool drawn = FALSE;
    static const char *catNames[3] = { "ЗБРОЯ", "ЛІКИ", "ІНШЕ" };
    u16 i;
    if (!drawn)
    {
        UI_box(3, 3, 34, 22);
        UI_text(FC_BLUE, "ІНВЕНТАР", 14, 4);
        UI_text(FC_CYAN, "ЗБРОЯ:", 4, 6);
        if (inventory.weapon >= 0 && inventory.item[inventory.weapon] != 0xFF)
            UI_text(FC_WHITE, itemDefs[inventory.item[inventory.weapon]].name, 11, 6);
        else
            UI_text(FC_WHITE, "НІМА (КУЛАКИ)", 11, 6);
        for (i = 0; i < INV_SLOTS; i++)
        {
            UI_clearRect(4, 7 + i, 32, 1);
            if (inventory.item[i] != 0xFF)
            {
                char num[3];
                num[0] = 'x';
                num[1] = '0' + inventory.count[i];
                num[2] = 0;
                UI_text(FC_WHITE, itemDefs[inventory.item[i]].name, 6, 7 + i);
                UI_text(FC_CYAN, num, 28, 7 + i);
            }
        }
        UI_text(FC_CYAN, "A ВИКОРИСТИ  B ЗАКРИТИ", 6, 21);
        /* cursor */
        UI_text(FC_WHITE, ">", 5, 7 + invCursor);
        drawn = TRUE;
    }

    if (joyPressedMask & BUTTON_UP)
    {
        invCursor = (invCursor == 0) ? INV_SLOTS - 1 : invCursor - 1;
        SFX_play(SFX_MENU);
        drawn = FALSE;
    }
    if (joyPressedMask & BUTTON_DOWN)
    {
        invCursor = (invCursor + 1) % INV_SLOTS;
        SFX_play(SFX_MENU);
        drawn = FALSE;
    }
    if (joyPressedMask & BUTTON_B || joyPressedMask & BUTTON_C ||
        joyPressedMask & BUTTON_START)
    {
        UI_clearRect(3, 3, 34, 22);
        gameState = GS_PLAY;
        drawn = FALSE;
    }
    else if (joyPressedMask & BUTTON_A)
    {
        if (inventory.item[invCursor] != 0xFF)
            INV_useSlot(&inventory, invCursor);
        drawn = FALSE;  /* redraw */
    }
    (void)catNames;
}

/* ---------------------------------------------------------------- interact prompt */
static void promptUpdate(void)
{
    s16 idx;
    if (player.dead || gameState != GS_PLAY)
        return;
    if (M01_checkPickupAt(player.u_q8, player.v_q8, 384, &idx))
    {
        const ItemSpawnDef *ps = &world_pickups[idx];
        HUD_setPrompt("[A] ВЗЯТИ");
        return;
    }
    if (M01_checkObjectAt(player.u_q8, player.v_q8, 640, &idx))
    {
        const ObjectDef *od = &world_objects[idx];
        if (od->type == OBJ_RADIO_END)
            HUD_setPrompt("[A] УВІМКНУТИ РАДІО");
        else
            HUD_setPrompt("[A] ШУКАТИ");
        return;
    }
    HUD_setPrompt(NULL);
}

/* ---------------------------------------------------------------- mission update (GS_PLAY) */
void M01_update(void)
{
    frameCount++;

    PLR_update(&player);
    ENY_updateAll();
    updatePickupSprites();
    CAM_update();
    zoneUpdate();
    dangerUpdate();
    promptUpdate();
    HUD_update();
    SFX_update();

    /* game over check */
    if (player.dead)
    {
        static u16 deathTimer = 0;
        deathTimer++;
        if (deathTimer == 1)
            HUD_message("ВИ ЗАГИНУЛИ...", "ОБОЛОНЬ ЗАЛИШАЄТЬСЯ НІМИМ.");
        if (deathTimer > 240)
        {
            deathTimer = 0;
            gameState = GS_GAMEOVER;
        }
    }
}
