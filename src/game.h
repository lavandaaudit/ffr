#ifndef GAME_H_
#define GAME_H_

/*
 * IBONARIUM: KYIV — THE LAST SIGNAL
 * MISSION 01: ОБОЛОНЬ — ПЕРШИЙ ДЕНЬ
 * Central game types & constants.
 *
 * World space: isometric grid. Entity positions are stored in "cell space"
 * as 8.8 fixed point (u_q8, v_q8). Screen projection:
 *   sx = (u - v) * 16 + PROJ_OFF_X - cam_x
 *   sy = (u + v) * 8  + PROJ_OFF_Y - cam_y
 */

#include <genesis.h>
#include "world_data.h"

/* ---------------------------------------------------------------- VRAM map */
#define MAP_BASE_INDEX      96      /* world map tileset: 96..~845 */
#define FONT_BASE           860     /* 4 font colors x 128 tiles: 860..1467 */
#define FONT_SLOT           128
#define HUD_TILES_BASE      1480    /* hud bar/box tiles: 1480..1520 */

/* font colors (index into font tile blocks) */
#define FC_WHITE    0
#define FC_BLUE     1
#define FC_RED      2
#define FC_CYAN     3

/* ---------------------------------------------------------------- items */
typedef enum {
    I_KNIFE = 0,
    I_WATER,
    I_MEDKIT,
    I_BANDAGE,
    I_PARTS,
    I_BATTERY,
    ITEM_COUNT
} ItemType;

/* item categories */
#define IC_WEAPON   0
#define IC_MED      1
#define IC_MISC     2

typedef struct {
    const char *name;       /* short display name */
    const char *desc;       /* description line (inventory) */
    u8 category;            /* IC_* */
    u8 maxStack;
} ItemDef;

extern const ItemDef itemDefs[ITEM_COUNT];

/* ---------------------------------------------------------------- inventory */
#define INV_SLOTS   12

typedef struct {
    u8 item[INV_SLOTS];
    u8 count[INV_SLOTS];
    s8 weapon;              /* slot index of equipped weapon or -1 */
} Inventory;

void INV_init(Inventory *inv);
s8   INV_add(Inventory *inv, ItemType it, u8 count);   /* returns slots used, -1 if full */
u8   INV_count(Inventory *inv, ItemType it);
bool INV_useSlot(Inventory *inv, u8 slot);             /* use consumable in slot */

/* ---------------------------------------------------------------- player */
#define AN_M_IDLE_F   0
#define AN_M_WALK_F   1
#define AN_M_ATK_F    2
#define AN_M_HURT     3
#define AN_M_DEAD     4
#define AN_M_IDLE_B   5
#define AN_M_WALK_B   6
#define AN_M_ATK_B    7
#define AN_M_PICKUP   8
#define AN_M_USE      9

/* 8 screen directions; movement vectors in cell space (du,dv) */
/* 0=N 1=NE 2=E 3=SE 4=S 5=SW 6=W 7=NW */
extern const s8 DIR_DU[8];
extern const s8 DIR_DV[8];

typedef struct {
    s16 u_q8, v_q8;         /* position in cell space (8.8) */
    s16 du_q8, dv_q8;       /* last move delta (for collision + facing) */
    u8 dir;                 /* facing DIR_* */
    u8 anim;                /* AN_M_* */
    u8 frame;
    u8 animTimer;
    bool flip;
    bool moving;
    bool running;
    Sprite *spr;

    /* survival stats 0..100 */
    s16 hp, sta, hunger, thirst, injury;
    s16 hungerAcc, thirstAcc, healAcc, injHealAcc, staAcc;

    u8 attackTimer;         /* frames until attack resolves / ends */
    u8 attackCd;            /* frames until next attack allowed */
    u8 invuln;              /* invulnerability frames after hit */
    u8 stepTimer;           /* footstep sfx timer */
    bool dead;
} Player;

void PLR_init(Player *p, s16 u, s16 v);
void PLR_update(Player *p);
void PLR_damage(Player *p, s16 dmg, s16 injChance, s16 injAmt, s16 knockDu, s16 knockDv);
void PLR_setAnim(Player *p, u8 anim, bool force);

/* projection helpers (shared) */
s16 worldToScreenX(s16 u_q8, s16 v_q8);
s16 worldToScreenY(s16 u_q8, s16 v_q8);

/* ---------------------------------------------------------------- enemies */
#define EN_WALKER   0
#define EN_RUNNER   1
#define EN_SCREAMER 2
#define EN_BRUTE    3

/* animations per type: 0=walk 1=attack 2=scream(screamer only)/hurt(brute) 3=dead */
#define AN_E_WALK   0
#define AN_E_ATK    1
#define AN_E_SPEC   2   /* screamer scream / brute hurt */
#define AN_E_DEAD   3

#define EST_IDLE    0
#define EST_CHASE   1
#define EST_ATTACK  2
#define EST_LOST    3
#define EST_SCREAM  4
#define EST_STUN    5
#define EST_DEAD    6

typedef struct {
    u8 type;
    u8 state;
    s16 u_q8, v_q8;
    s16 homeU, homeV;       /* spawn point (patrol anchor), 8.8 */
    s16 hp;
    u8 dir;
    u8 animTimer;
    u8 atkTimer;            /* attack windup/cooldown counter */
    u8 stunTimer;
    u8 lostTimer;
    u8 screamed;            /* screamer used its scream */
    bool flip;
    bool active;
    Sprite *spr;
} Enemy;

typedef struct {
    s16 maxHp;
    s16 dmg;
    s16 speed_q8;           /* cells/frame 8.8 */
    s16 aggroCells_q8;      /* sight radius in cells 8.8 */
    s16 injChance;          /* % chance to cause injury on hit */
    s16 injAmt;
    u8 atkWindup;           /* frames before hit lands */
    u8 atkCooldown;         /* total frames between attacks */
    u8 sheetRowDead;       /* AN_E_DEAD anim index */
} EnemyDef;

extern const EnemyDef enemyDefs[4];

#define MAX_ENEMIES 12
extern Enemy enemies[MAX_ENEMIES];
extern u16 enemyCount;

void ENY_spawnAll(void);
void ENY_updateAll(void);
void ENY_damage(Enemy *e, s16 dmg, s16 kdu, s16 kdv);
void ENY_alertRadius(s16 u_q8, s16 v_q8, s16 radiusCells_q8);
bool ENY_anyAggroNear(s16 u_q8, s16 v_q8, s16 radiusCells_q8);

/* ---------------------------------------------------------------- combat */
void CBT_init(void);
void CBT_playerAttack(Player *p);       /* called on B press */
void CBT_update(Player *p);             /* resolves attack frames */
void CBT_enemyHitPlayer(Enemy *e, Player *p);

/* ---------------------------------------------------------------- camera */
typedef struct {
    s16 x, y;               /* current scroll (map pixels) */
    s16 tx, ty;              /* target */
} Camera;

extern Camera camera;
void CAM_update(void);
void CAM_snap(void);

/* ---------------------------------------------------------------- world/map */
void WORLD_load(void);
Map *worldMapGet(void);
u8   WORLD_isBlockedCell(s16 u, s16 v);     /* integer cell coords */
u8   WORLD_isBlocked(s16 u_q8, s16 v_q8);   /* 8.8 coords; tests center+corners */
void WORLD_posToScreen(const Player *p, s16 *sx, s16 *sy);

/* ---------------------------------------------------------------- HUD */
void HUD_init(void);
void HUD_update(void);
void HUD_drawBars(void);
void HUD_setTask(const char *text);      /* NULL = clear */
void HUD_flashTaskUpdate(void);
void HUD_message(const char *line1, const char *line2);
void HUD_setPrompt(const char *text);    /* NULL = hide */
void HUD_clearAll(void);

/* messages/queues live in hud.c */
void HUD_updateMessages(void);

/* ---------------------------------------------------------------- ui (font/text) */
void UI_init(void);
void UI_text(u16 color, const char *s, u16 x, u16 y);
void UI_textLen(u16 color, const char *s, u16 x, u16 y, u16 maxlen);
void UI_clearRect(u16 x, u16 y, u16 w, u16 h);
void UI_box(u16 x, u16 y, u16 w, u16 h);       /* panel box using hud tiles */
u16  UI_charTile(u16 color, u8 c);             /* tile attr for charcode */

/* ---------------------------------------------------------------- sfx */
/* ambient sound modes */
#define AMB_NONE  0
#define AMB_WIND  1
#define AMB_RADIO 2

/* joypad edge mask (updated in main loop) */
extern u16 joyPressedMask;

enum {
    SFX_NONE = 0,
    SFX_PICKUP,
    SFX_KNIFE,
    SFX_HIT,
    SFX_GROAN,
    SFX_SCREAM,
    SFX_BRUTE,
    SFX_DOOR,
    SFX_STEP,
    SFX_USE,
    SFX_HURT,
    SFX_MENU,
    SFX_SAVE
};
void SFX_init(void);
void SFX_play(u16 id);
void SFX_update(void);
void SFX_footstep(void);
void SFX_setAmbient(u16 mode);      /* 0 none 1 wind 2 radio static */
void SFX_stopAll(void);

/* morse signal player for ending (blocking-ish sequence driven by mission01) */
void SFX_signalBeep(u16 pattern);

/* ---------------------------------------------------------------- save */
typedef struct {
    u32 magic;
    u16 version;
    s16 u_q8, v_q8;
    s16 hp, sta, hunger, thirst, injury;
    u8 invItem[INV_SLOTS];
    u8 invCount[INV_SLOTS];
    s8 weapon;
    u8 taskIdx;
    u32 flags;              /* looted flags: pickups(0..3) + objects(4..) */
    u8 missionDone;
    u16 checksum;
} SaveData;

bool SAVE_exists(void);
bool SAVE_load(SaveData *out);
void SAVE_write(const SaveData *data);
u16  SAVE_computeChecksum(const SaveData *d);

/* ---------------------------------------------------------------- mission 01 */
enum {
    TASK_NONE = 0,
    TASK_WATER,
    TASK_KNIFE,
    TASK_EXIT,
    TASK_DANGER,
    TASK_SIGNAL
};

#define MAX_TASK        TASK_SIGNAL
#define GAMEFLAG_PICKUP_BASE    0   /* bits 0..3 pickups */
#define GAMEFLAG_OBJECT_BASE    4   /* bits 4.. object looted */

typedef enum {
    GS_TITLE = 0,
    GS_INTRO,
    GS_PLAY,
    GS_PAUSE,
    GS_INVENTORY,
    GS_CUTSCENE,        /* radio ending sequence */
    GS_END,
    GS_GAMEOVER
} GameState;

extern GameState gameState;
extern Player player;
extern Inventory inventory;
extern SaveData saveData;
extern u32 gameFlags;
extern u8 currentTask;
extern bool missionComplete;
extern u16 frameCount;

void M01_initNewGame(void);
void M01_update(void);          /* play-state logic: tasks, interactions */
void M01_tryInteract(void);
void M01_startEnding(void);
void M01_introUpdate(void);
void M01_cutsceneUpdate(void);
void M01_loadFromSave(void);
void M01_buildSaveData(SaveData *d);
bool M01_checkPickupAt(s16 u_q8, s16 v_q8, s16 range_q8, s16 *idx);
bool M01_checkObjectAt(s16 u_q8, s16 v_q8, s16 range_q8, s16 *idx);

/* pause/inventory screens */
void UI_pauseScreen(void);
void UI_inventoryScreen(void);

/* helpers for flags */
bool M01_flagPickup(u16 idx);
void M01_setFlagPickup(u16 idx);
bool M01_flagObject(u16 idx);
void M01_setFlagObject(u16 idx);

void setTask(u8 task);

#endif /* GAME_H_ */
