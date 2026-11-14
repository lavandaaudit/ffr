/*
 * player.c — Marco: 8-direction isometric movement, survival stats,
 * animation state machine, footstep noise.
 *
 * Controls (classic layout):
 *   D-pad — move (8 directions on iso grid)
 *   B     — attack (knife / punch)
 *   A     — interact / search / pick up
 *   C     — tap: inventory, hold: run
 *   START — pause
 */
#include "game.h"
#include "resources.h"

const s8 DIR_DU[8] = { -1,  0,  1,  1, 1, 0, -1, -1 };
const s8 DIR_DV[8] = { -1, -1, -1,  0, 1, 1,  1,  0 };

#define WALK_SPEED_Q8   44      /* ~2.6 cells/s */
#define RUN_SPEED_Q8    74      /* ~4.4 cells/s */
#define RADIUS_Q8       51      /* collision radius ~0.2 cell */

#define STAMINA_RUN_DRAIN   77  /* Q8 per frame (~18/s) */
#define STAMINA_REGEN       43  /* ~10/s */
#define STAMINA_ATK_COST    12

#define HUNGER_DRAIN_ACC    2   /* Q8/frame -> 100 in ~37 min */
#define THIRST_DRAIN_ACC    3   /* -> 100 in ~27 min */
#define STARVE_HP_ACC       1   /* hp -1 every ~8s when starving */
#define DEHYD_HP_ACC        1   /* hp -1 every ~5s when dehydrated (x2 acc) */
#define INJ_HEAL_ACC        1   /* injury -1 every ~25s */

static u16 cHeldPrev = 0;
static u16 cHoldTimer = 0;

void PLR_init(Player *p, s16 u, s16 v)
{
    memset(p, 0, sizeof(Player));
    p->u_q8 = u << 8;
    p->v_q8 = v << 8;
    p->dir = 4;
    p->hp = 100;
    p->sta = 100;
    p->hunger = 100;
    p->thirst = 100;
    p->injury = 0;
    p->spr = SPR_addSprite(&spr_marco,
                           worldToScreenX(p->u_q8, p->v_q8) - camera.x - 16,
                           worldToScreenY(p->u_q8, p->v_q8) - camera.y - 26,
                           TILE_ATTR(PAL2, FALSE, FALSE, FALSE));
    SPR_setAnimAndFrame(p->spr, AN_M_IDLE_F, 0);
    SPR_setZ(p->spr, worldToScreenY(p->u_q8, p->v_q8));
    cHeldPrev = 0;
    cHoldTimer = 0;
}

static bool try_move(Player *p, s16 du, s16 dv)
{
    s16 nu = p->u_q8 + du;
    s16 nv = p->v_q8 + dv;
    if (WORLD_isBlocked(nu, nv))
        return FALSE;
    p->u_q8 = nu;
    p->v_q8 = nv;
    return TRUE;
}

void PLR_setAnim(Player *p, u8 anim, bool force)
{
    if (p->anim != anim || force)
    {
        p->anim = anim;
        p->frame = 0;
        p->animTimer = 0;
        SPR_setAnimAndFrame(p->spr, anim, 0);
    }
}

static void set_facing(Player *p, u8 dir)
{
    p->dir = dir;
    bool back = (dir == 0 || dir == 1 || dir == 2 || dir == 7);  /* N NE E NW */
    bool flip = (dir == 2 || dir == 3 || dir == 6 || dir == 5);  /* E SE W SW */
    p->flip = flip;
    SPR_setHFlip(p->spr, flip);
}

/* speed modified by injury (up to -45%) */
static s16 effectiveSpeed(Player *p, s16 base)
{
    s16 inj = p->injury > 60 ? 60 : p->injury;
    return base - ((base * inj * 3) >> 8);   /* inj 60 -> -70% of base*0.7 */
}

void PLR_update(Player *p)
{
    u16 joy = JOY_readJoypad(JOY_1);
    u8 dirIdx;
    bool moved = FALSE;
    s16 spd;

    /* ---- run via C hold ---- */
    bool cNow = (joy & BUTTON_C) != 0;
    bool running = FALSE;
    if (cNow && cHeldPrev)
    {
        cHoldTimer++;
        if (cHoldTimer > 12 && (joy & (BUTTON_UP | BUTTON_DOWN | BUTTON_LEFT | BUTTON_RIGHT)))
            running = TRUE;
    }
    else
    {
        cHoldTimer = 0;
        if (cNow && !cHeldPrev)
        {
            /* quick tap -> open inventory (handled in main.c via tap event) */
        }
    }
    if (running && (p->sta <= 5 || p->attackTimer > 0))
        running = FALSE;
    p->running = running;

    /* ---- movement ---- */
    if (!p->dead && p->attackTimer == 0)
    {
        u8 du = ((joy & BUTTON_RIGHT) ? 1 : 0) | ((joy & BUTTON_LEFT) ? 2 : 0);
        u8 dv = ((joy & BUTTON_DOWN) ? 1 : 0) | ((joy & BUTTON_UP) ? 2 : 0);
        if (du == 0 && dv == 0)
        {
            dirIdx = 0xFF;
        }
        else
        {
            u8 idx = (dv << 2) | du;    /* 0..12 sparse */
            dirIdx = 0xFF;
            /* explicit mapping (du,dv) -> dir */
            if (du == 1 && dv == 0) dirIdx = 2;      /* E */
            else if (du == 2 && dv == 0) dirIdx = 6; /* W */
            else if (du == 0 && dv == 1) dirIdx = 4; /* S */
            else if (du == 0 && dv == 2) dirIdx = 0; /* N */
            else if (du == 1 && dv == 1) dirIdx = 3; /* SE */
            else if (du == 1 && dv == 2) dirIdx = 1; /* NE */
            else if (du == 2 && dv == 1) dirIdx = 5; /* SW */
            else if (du == 2 && dv == 2) dirIdx = 7; /* NW */
        }

        if (dirIdx != 0xFF)
        {
            set_facing(p, dirIdx);
            spd = effectiveSpeed(p, running ? RUN_SPEED_Q8 : WALK_SPEED_Q8);
            s16 du_q8 = ((s16)DIR_DU[dirIdx] * spd);
            s16 dv_q8 = ((s16)DIR_DV[dirIdx] * spd);
            /* axis-separated move for wall sliding */
            if (du_q8 != 0 && !try_move(p, du_q8, 0))
                du_q8 = 0;
            if (dv_q8 != 0 && !try_move(p, 0, dv_q8))
                dv_q8 = 0;
            moved = (du_q8 != 0 || dv_q8 != 0);
            p->du_q8 = du_q8;
            p->dv_q8 = dv_q8;
        }
        else
        {
            p->du_q8 = 0;
            p->dv_q8 = 0;
        }
    }
    else
    {
        p->du_q8 = 0;
        p->dv_q8 = 0;
    }
    p->moving = moved;

    /* ---- stamina ---- */
    if (running)
    {
        p->staAcc += STAMINA_RUN_DRAIN;         /* 77 = ~18/s drain */
        while (p->staAcc >= 256)
        {
            p->staAcc -= 256;
            p->sta--;
        }
        if (p->sta < 0) { p->sta = 0; p->staAcc = 0; }
    }
    else
    {
        p->staAcc -= STAMINA_REGEN;             /* 43 = ~10/s regen */
        while (p->staAcc < 0)
        {
            p->staAcc += 256;
            p->sta++;
        }
        if (p->sta > 100) { p->sta = 100; p->staAcc = 0; }
    }

    /* ---- hunger / thirst / starvation ---- */
    p->hungerAcc += HUNGER_DRAIN_ACC;
    while (p->hungerAcc >= 256) { p->hungerAcc -= 256; p->hunger--; }
    p->thirstAcc += THIRST_DRAIN_ACC;
    while (p->thirstAcc >= 256) { p->thirstAcc -= 256; p->thirst--; }
    if (p->hunger < 0) p->hunger = 0;
    if (p->thirst < 0) p->thirst = 0;

    if (p->hunger == 0)
    {
        p->healAcc += STARVE_HP_ACC;
        if (p->healAcc >= 480) { p->healAcc -= 480; p->hp--; }
    }
    if (p->thirst == 0)
    {
        p->healAcc += DEHYD_HP_ACC * 2;
        if (p->healAcc >= 480) { p->healAcc -= 480; p->hp--; }
    }

    /* ---- injury slow recovery ---- */
    if (p->injury > 0)
    {
        p->injHealAcc += INJ_HEAL_ACC;
        if (p->injHealAcc >= 1500) { p->injHealAcc -= 1500; p->injury--; }
    }

    /* ---- footstep noise & sfx ---- */
    if (moved)
    {
        p->stepTimer--;
        if (p->stepTimer == 0)
        {
            p->stepTimer = running ? 8 : 14;
            SFX_footstep();
            /* running is loud: alert nearby enemies */
            ENY_alertRadius(p->u_q8, p->v_q8, running ? (6 << 8) : (3 << 8));
        }
    }
    else
        p->stepTimer = 4;

    /* ---- timers ---- */
    if (p->attackCd > 0) p->attackCd--;
    if (p->invuln > 0) p->invuln--;
    CBT_update(p);

    /* ---- death ---- */
    if (p->hp <= 0 && !p->dead)
    {
        p->hp = 0;
        p->dead = TRUE;
        PLR_setAnim(p, AN_M_DEAD, TRUE);
        SFX_play(SFX_HURT);
    }

    /* ---- animation select ---- */
    if (!p->dead)
    {
        if (p->attackTimer > 0)
        {
            /* handled by combat: anim frames set there */
        }
        else if (moved)
            PLR_setAnim(p, (p->dir == 0 || p->dir == 1 || p->dir == 2 || p->dir == 7)
                          ? AN_M_WALK_B : AN_M_WALK_F, FALSE);
        else
            PLR_setAnim(p, (p->dir == 0 || p->dir == 1 || p->dir == 2 || p->dir == 7)
                          ? AN_M_IDLE_B : AN_M_IDLE_F, FALSE);
    }

    /* ---- animate frame counter (manual timing) ---- */
    p->animTimer++;
    {
        u8 animSpeed = (p->anim == AN_M_WALK_F || p->anim == AN_M_WALK_B)
                       ? (p->running ? 5 : 8) : 10;
        if (p->animTimer >= animSpeed)
        {
            p->animTimer = 0;
            p->frame = (p->frame + 1) & 3;
            SPR_setFrame(p->spr, p->frame);
        }
    }

    /* ---- sprite position & depth ---- */
    {
        s16 sx = worldToScreenX(p->u_q8, p->v_q8);
        s16 sy = worldToScreenY(p->u_q8, p->v_q8);
        SPR_setPosition(p->spr, sx - camera.x - 16, sy - camera.y - 26);
        SPR_setZ(p->spr, sy);
        /* hurt flash: toggle visibility quickly */
        if (p->invuln > 0 && !p->dead)
            SPR_setVisibility(p->spr, (p->invuln & 4) ? VISIBLE : HIDDEN);
        else
            SPR_setVisibility(p->spr, VISIBLE);
    }

    cHeldPrev = cNow;
}

void PLR_damage(Player *p, s16 dmg, s16 injChance, s16 injAmt, s16 knockDu, s16 knockDv)
{
    if (p->dead || p->invuln > 0)
        return;

    p->hp -= dmg;
    p->invuln = 45;
    SFX_play(SFX_HURT);

    /* knockback */
    {
        s16 kdu = knockDu >> 1;
        s16 kdv = knockDv >> 1;
        if (!WORLD_isBlocked(p->u_q8 + kdu, p->v_q8 + kdv))
        {
            p->u_q8 += kdu;
            p->v_q8 += kdv;
        }
    }

    /* injury chance */
    if (injChance > 0)
    {
        u16 roll = (frameCount + (u16)random()) % 100;
        if (roll < (u16)injChance)
        {
            p->injury += injAmt;
            if (p->injury > 100) p->injury = 100;
            HUD_message("ВИ ПОРАНЕНИЙ! ВИКОРИСТАЙТЕ БИНТ.", NULL);
        }
        else
            HUD_message("ВИ ОТРИМАЛИ УДАР!", NULL);
    }

    if (p->hp <= 0)
    {
        p->hp = 0;
        p->dead = TRUE;
        PLR_setAnim(p, AN_M_DEAD, TRUE);
    }
}
