/*
 * enemy.c — infected AI: WALKER / RUNNER / SCREAMER / BRUTE.
 *
 * States: idle (wander near spawn) -> chase (sees/noises player) ->
 * attack (in range, windup then hit) -> lost (target lost) -> dead.
 * SCREAMER screams on first aggro, alerting every infected nearby.
 * All positions are 8.8 cell-space; movement uses wall sliding.
 */
#include "game.h"
#include "resources.h"

Enemy enemies[MAX_ENEMIES];
u16 enemyCount = 0;

const EnemyDef enemyDefs[4] = {
    /* EN_WALKER */
    { 30,  8,  8, (5 << 8), 25, 12, 30, 90, AN_E_DEAD },
    /* EN_RUNNER */
    { 25, 10, 16, (7 << 8), 30, 14, 22, 70, AN_E_DEAD },
    /* EN_SCREAMER */
    { 20,  6, 12, (8 << 8), 20, 10, 25, 80, AN_E_DEAD },
    /* EN_BRUTE */
    { 130, 24, 6, (6 << 8), 50, 25, 36, 110, AN_E_DEAD },
};

void ENY_spawnAll(void)
{
    u16 i;
    enemyCount = WORLD_ENEMY_COUNT;
    for (i = 0; i < enemyCount && i < MAX_ENEMIES; i++)
    {
        Enemy *e = &enemies[i];
        const EnemySpawnDef *sd = &world_enemies[i];
        const EnemyDef *def = &enemyDefs[sd->type];

        e->type = sd->type;
        e->state = EST_IDLE;
        e->u_q8 = sd->u << 8;
        e->v_q8 = sd->v << 8;
        e->homeU = e->u_q8;
        e->homeV = e->v_q8;
        e->hp = def->maxHp;
        e->dir = 4;
        e->animTimer = 0;
        e->atkTimer = 0;
        e->stunTimer = 0;
        e->lostTimer = 0;
        e->screamed = 0;
        e->flip = FALSE;
        e->active = TRUE;

        e->spr = SPR_addSprite(sd->type == EN_WALKER ? &spr_walker :
                               sd->type == EN_RUNNER ? &spr_runner :
                               sd->type == EN_SCREAMER ? &spr_screamer :
                               &spr_brute,
                               worldToScreenX(e->u_q8, e->v_q8) - camera.x - 16,
                               worldToScreenY(e->u_q8, e->v_q8) - camera.y - 26,
                               TILE_ATTR(PAL3, FALSE, FALSE, FALSE));
        SPR_setAnimAndFrame(e->spr, AN_E_WALK, 0);
        SPR_setFrame(e->spr, 0);
        SPR_setZ(e->spr, worldToScreenY(e->u_q8, e->v_q8));
        SPR_setVisibility(e->spr, VISIBLE);
    }
}

void ENY_releaseAll(void)
{
    u16 i;
    for (i = 0; i < enemyCount; i++)
    {
        if (enemies[i].spr)
        {
            SPR_releaseSprite(enemies[i].spr);
            enemies[i].spr = NULL;
        }
    }
    enemyCount = 0;
}

static bool try_move_e(Enemy *e, s16 du, s16 dv)
{
    s16 nu = e->u_q8 + du;
    s16 nv = e->v_q8 + dv;
    if (WORLD_isBlocked(nu, nv))
        return FALSE;
    e->u_q8 = nu;
    e->v_q8 = nv;
    return TRUE;
}

static s16 dist_q8(Enemy *e, Player *p)
{
    s32 du = (s32)p->u_q8 - e->u_q8;
    s32 dv = (s32)p->v_q8 - e->v_q8;
    /* manhattan distance is fine for iso logic */
    s32 d = (du < 0 ? -du : du) + (dv < 0 ? -dv : dv);
    if (d > 0x7FFF) d = 0x7FFF;
    return (s16)d;
}

static void set_dir_to(Enemy *e, s16 du, s16 dv)
{
    /* pick facing from dominant axis */
    if (abs(du) > abs(dv))
        e->flip = (du < 0);
    else if (abs(dv) > 0)
        e->flip = (dv < 0);
    SPR_setHFlip(e->spr, e->flip);
}

static void start_attack(Enemy *e, const EnemyDef *def, Player *p)
{
    e->state = EST_ATTACK;
    e->atkTimer = def->atkWindup;
    SPR_setAnim(e->spr, AN_E_ATK);
    SPR_setFrame(e->spr, 0);
    e->animTimer = 0;
}

static void do_wander(Enemy *e, const EnemyDef *def)
{
    /* slow shamble around home */
    u16 r = (frameCount + (u16)(e->u_q8 ^ e->v_q8)) & 63;
    if (r == 0)
    {
        s16 du = (s16)(((u16)random() & 7) - 3);
        s16 dv = (s16)(((u16)random() & 7) - 3);
        if (!WORLD_isBlocked(e->u_q8 + du * 128, e->v_q8 + dv * 128))
        {
            if (!try_move_e(e, du * 32, dv * 32))
                try_move_e(e, du * 32, 0);
            set_dir_to(e, du, dv);
        }
    }
    /* drift back home if too far */
    {
        s32 hu = (s32)e->homeU - e->u_q8;
        s32 hv = (s32)e->homeV - e->v_q8;
        if (hu > 2 << 8) try_move_e(e, def->speed_q8 >> 1, 0);
        else if (hu < -(2 << 8)) try_move_e(e, -(def->speed_q8 >> 1), 0);
        else if (hv > 2 << 8) try_move_e(e, 0, def->speed_q8 >> 1);
        else if (hv < -(2 << 8)) try_move_e(e, 0, -(def->speed_q8 >> 1));
    }
}

static void chase_move(Enemy *e, const EnemyDef *def, Player *p)
{
    s32 du = (s32)p->u_q8 - e->u_q8;
    s32 dv = (s32)p->v_q8 - e->v_q8;
    s16 step = def->speed_q8;

    set_dir_to(e, du, dv);

    /* normalize: move at most step in combined direction (axis-split w/ slide) */
    s16 mx = 0, my = 0;
    if (du > step) mx = step; else if (du < -step) mx = -step; else mx = (s16)du;
    if (dv > step) my = step; else if (dv < -step) my = -step; else my = (s16)dv;

    bool moved = FALSE;
    if (mx != 0)
    {
        if (try_move_e(e, mx, 0)) moved = TRUE;
        else if (my != 0 && try_move_e(e, 0, my > 0 ? step : -step)) moved = TRUE;
    }
    if (my != 0 && !moved)
    {
        if (try_move_e(e, 0, my)) moved = TRUE;
        else if (mx != 0 && try_move_e(e, mx > 0 ? step : -step, 0)) moved = TRUE;
    }
    if (mx == 0 && my == 0) moved = FALSE;
}

void ENY_alertRadius(s16 u_q8, s16 v_q8, s16 radiusCells_q8)
{
    u16 i;
    for (i = 0; i < enemyCount; i++)
    {
        Enemy *e = &enemies[i];
        const EnemyDef *def;
        s16 d;
        if (e->state == EST_DEAD || !e->active) continue;
        def = &enemyDefs[e->type];
        d = abs((s32)u_q8 - e->u_q8) + abs((s32)v_q8 - e->v_q8);
        if (d <= radiusCells_q8 && e->state == EST_IDLE)
        {
            e->state = EST_CHASE;
            e->lostTimer = 0;
            if (e->type != EN_SCREAMER || e->screamed)
                SFX_play(SFX_GROAN);
        }
    }
}

void ENY_alertAll(void)
{
    u16 i;
    for (i = 0; i < enemyCount; i++)
    {
        if (enemies[i].state != EST_DEAD)
        {
            enemies[i].state = EST_CHASE;
            enemies[i].lostTimer = 0;
        }
    }
}

bool ENY_anyAggroNear(s16 u_q8, s16 v_q8, s16 radiusCells_q8)
{
    u16 i;
    for (i = 0; i < enemyCount; i++)
    {
        Enemy *e = &enemies[i];
        if (e->state == EST_DEAD || e->state == EST_IDLE) continue;
        if (abs((s32)u_q8 - e->u_q8) + abs((s32)v_q8 - e->v_q8) <= radiusCells_q8)
            return TRUE;
    }
    return FALSE;
}

void ENY_damage(Enemy *e, s16 dmg, s16 kdu, s16 kdv)
{
    const EnemyDef *def;
    if (e->state == EST_DEAD) return;
    def = &enemyDefs[e->type];

    e->hp -= dmg;
    SFX_play(e->type == EN_BRUTE ? SFX_BRUTE : SFX_HIT);

    /* knockback */
    {
        s16 ku = kdu >> 1;
        s16 kv = kdv >> 1;
        if (!WORLD_isBlocked(e->u_q8 + ku, e->v_q8 + kv))
        {
            e->u_q8 += ku;
            e->v_q8 += kv;
        }
    }

    if (e->hp <= 0)
    {
        e->state = EST_DEAD;
        SPR_setAnim(e->spr, def->sheetRowDead);
        SPR_setFrame(e->spr, 0);
        /* blood decal on sprite: keep corpse visible */
        if (e->type == EN_BRUTE)
            HUD_message("BRUTE ПАДАЄ. ВЕЛИКЕ ТІЛО НЕ РУХАЄТЬСЯ.", NULL);
        return;
    }

    /* stagger: interrupt attack */
    if (e->state == EST_ATTACK)
        e->state = EST_CHASE;
    if (e->type == EN_BRUTE)
    {
        SPR_setAnim(e->spr, AN_E_SPEC);   /* hurt anim */
        SPR_setFrame(e->spr, 0);
        e->stunTimer = 24;
    }
    else
    {
        e->state = EST_CHASE;
        e->lostTimer = 0;
    }
}

void ENY_updateAll(void)
{
    u16 i;
    for (i = 0; i < enemyCount; i++)
    {
        Enemy *e = &enemies[i];
        const EnemyDef *def = &enemyDefs[e->type];
        s16 d;

        if (e->state == EST_DEAD)
        {
            SPR_setPosition(e->spr,
                            worldToScreenX(e->u_q8, e->v_q8) - camera.x - 16,
                            worldToScreenY(e->u_q8, e->v_q8) - camera.y - 26);
            continue;
        }
        if (!e->active) continue;

        d = dist_q8(e, &player);

        /* ---- stun (brute stagger) ---- */
        if (e->stunTimer > 0)
        {
            e->stunTimer--;
            if (e->stunTimer == 0)
            {
                e->state = EST_CHASE;
                SPR_setAnim(e->spr, AN_E_WALK);
            }
        }
        else if (e->state == EST_SCREAM)
        {
            e->animTimer++;
            if (e->animTimer > 60)
            {
                e->state = EST_CHASE;
                SPR_setAnim(e->spr, AN_E_WALK);
            }
        }
        else if (e->state == EST_IDLE)
        {
            /* sight check */
            if (d <= def->aggroCells_q8 && !player.dead)
            {
                e->state = EST_CHASE;
                e->lostTimer = 0;
                if (e->type == EN_SCREAMER && !e->screamed)
                {
                    e->screamed = 1;
                    e->state = EST_SCREAM;
                    e->animTimer = 0;
                    SPR_setAnim(e->spr, AN_E_SPEC);
                    SPR_setFrame(e->spr, 0);
                    SFX_play(SFX_SCREAM);
                    ENY_alertAll();
                    HUD_message("СКРИК! ЗАРАЖЕНІ ЗБІГАЮТЬСЯ...", NULL);
                }
                else
                    SFX_play(SFX_GROAN);
            }
            else
                do_wander(e, def);
        }
        else if (e->state == EST_CHASE || e->state == EST_LOST)
        {
            if (player.dead)
            {
                e->state = EST_IDLE;
                SPR_setAnim(e->spr, AN_E_WALK);
            }
            else if (d <= 384)
            {
                /* in attack range (manhattan < 1.5 cells) */
                start_attack(e, def, &player);
            }
            else if (d <= def->aggroCells_q8 * 2)
            {
                e->state = EST_CHASE;
                e->lostTimer = 0;
                chase_move(e, def, &player);
            }
            else
            {
                /* lost target */
                e->lostTimer++;
                if (e->lostTimer > 240)
                {
                    e->state = EST_IDLE;
                    SPR_setAnim(e->spr, AN_E_WALK);
                }
                else
                    chase_move(e, def, &player);
            }
        }
        else if (e->state == EST_ATTACK)
        {
            e->animTimer++;
            if (e->atkTimer > 0)
            {
                e->atkTimer--;
                if (e->atkTimer == 0)
                {
                    /* hit moment */
                    s16 d2 = dist_q8(e, &player);
                    if (d2 <= 11 * 256 && !player.dead)
                        CBT_enemyHitPlayer(e, &player);
                    SPR_setFrame(e->spr, 1);
                }
            }
            else
            {
                /* cooldown tail */
                if (e->animTimer > def->atkCooldown)
                {
                    e->state = EST_CHASE;
                    e->lostTimer = 0;
                    SPR_setAnim(e->spr, AN_E_WALK);
                    e->animTimer = 0;
                }
            }
        }

        /* walk anim frames */
        if (e->state == EST_CHASE || e->state == EST_LOST || e->state == EST_IDLE)
        {
            e->animTimer++;
            if (e->animTimer >= 10)
            {
                e->animTimer = 0;
                e->dir = (e->dir + 1) & 3;
                SPR_setFrame(e->spr, e->dir);
            }
        }
        else if (e->state == EST_SCREAM || e->state == EST_ATTACK)
        {
            e->animTimer++;
            if (e->animTimer >= 12)
            {
                e->animTimer = 0;
                e->dir = (e->dir + 1) & 1;
                SPR_setFrame(e->spr, e->dir);
            }
        }

        /* sprite pos + depth */
        {
            s16 sx = worldToScreenX(e->u_q8, e->v_q8);
            s16 sy = worldToScreenY(e->u_q8, e->v_q8);
            SPR_setPosition(e->spr, sx - camera.x - 16, sy - camera.y - 26);
            SPR_setZ(e->spr, sy);
        }
    }
}

/* random() helper — SGDK provides random() */
