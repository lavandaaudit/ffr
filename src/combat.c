/*
 * combat.c — attack resolution for player and enemies.
 */
#include "game.h"

#define KNIFE_DMG       22
#define STAMINA_ATK_COST 12
#define PUNCH_DMG       6
#define KNIFE_RANGE     640     /* manhattan q8: ~2.5 cells */
#define KNIFE_ARC       128     /* min dot component */
#define ATK_DURATION    24
#define ATK_HIT_FRAME   12

void CBT_init(void)
{
}

static bool inArc(Player *p, s32 du, s32 dv)
{
    /* facing vector in cell space */
    s32 fu = DIR_DU[(u8)p->dir];
    s32 fv = DIR_DV[(u8)p->dir];
    s32 len = abs(du) + abs(dv);
    if (len == 0) return TRUE;
    /* project target onto facing: dot of normalized-ish vectors */
    s32 dot = fu * du + fv * dv;
    /* allow wide arc: target must be mostly in front */
    return dot > 0 || len < 256;
}

void CBT_playerAttack(Player *p)
{
    if (p->dead || p->attackCd > 0 || p->attackTimer > 0)
        return;

    bool hasKnife = (inventory.weapon >= 0 && inventory.item[inventory.weapon] == I_KNIFE);

    if (p->sta < 5)
        return;

    if (hasKnife)
    {
        p->sta -= STAMINA_ATK_COST;
        if (p->sta < 0) p->sta = 0;
    }
    else if (p->sta < 3)
        return;
    else
        p->sta -= 6;

    p->attackTimer = ATK_DURATION;
    p->attackCd = ATK_DURATION + 8;

    /* facing anim */
    bool back = (p->dir == 0 || p->dir == 1 || p->dir == 2 || p->dir == 7);
    PLR_setAnim(p, back ? AN_M_ATK_B : AN_M_ATK_F, TRUE);
    p->frame = 0;
    SPR_setFrame(p->spr, 0);

    SFX_play(hasKnife ? SFX_KNIFE : SFX_MENU);
    /* attacks are loud */
    ENY_alertRadius(p->u_q8, p->v_q8, 5 << 8);
}

void CBT_update(Player *p)
{
    if (p->attackTimer == 0)
        return;

    p->attackTimer--;
    u8 elapsed = ATK_DURATION - p->attackTimer;

    /* strike moment */
    if (elapsed == ATK_HIT_FRAME)
    {
        bool hasKnife = (inventory.weapon >= 0 && inventory.item[inventory.weapon] == I_KNIFE);
        s16 dmg = hasKnife ? KNIFE_DMG : PUNCH_DMG;
        s16 range = hasKnife ? KNIFE_RANGE : 384;
        u16 i;
        bool hit = FALSE;

        for (i = 0; i < enemyCount; i++)
        {
            Enemy *e = &enemies[i];
            s32 du, dv;
            s32 d;
            if (e->state == EST_DEAD || !e->active) continue;
            du = (s32)e->u_q8 - p->u_q8;
            dv = (s32)e->v_q8 - p->v_q8;
            d = abs(du) + abs(dv);
            if (d <= range && inArc(p, du, dv))
            {
                ENY_damage(e, dmg, (s16)du, (s16)dv);
                hit = TRUE;
            }
        }
        if (hit && hasKnife)
            SPR_setFrame(p->spr, 1);
    }
    else if (elapsed == ATK_DURATION - 1)
    {
        SPR_setFrame(p->spr, 2);
    }
}

void CBT_enemyHitPlayer(Enemy *e, Player *p)
{
    const EnemyDef *def = &enemyDefs[e->type];
    s32 du = (s32)p->u_q8 - e->u_q8;
    s32 dv = (s32)p->v_q8 - e->v_q8;
    s16 kdu = du > 0 ? 128 : -128;
    s16 kdv = dv > 0 ? 128 : -128;
    if (e->type == EN_BRUTE)
    {
        kdu <<= 1;
        kdv <<= 1;
    }
    PLR_damage(p, def->dmg, def->injChance, def->injAmt, kdu, kdv);
}
