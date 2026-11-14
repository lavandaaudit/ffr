/*
 * world.c — isometric map loading, projection, collision.
 */
#include "game.h"
#include "resources.h"

static Map *worldMap = NULL;

Map *worldMapGet(void)
{
    return worldMap;
}

void WORLD_load(void)
{
    /* palette for BG plane 1 (map) */
    PAL_setPaletteColors(PAL1 * 16, &pal_world, CPU);
    /* backdrop = void color */
    PAL_setColor(0, pal_world.data[0]);

    if (worldMap != NULL)
    {
        MAP_release(worldMap);
        worldMap = NULL;
    }
    /* stream the big isometric map into BG_B; tiles start at MAP_BASE_INDEX,
       palette 1, low priority (sprites & HUD overlay them) */
    worldMap = MAP_create(&map_world, BG_B,
                          TILE_ATTR_FULL(PAL1, FALSE, FALSE, FALSE, MAP_BASE_INDEX));
    MAP_scrollTo(worldMap, 0, 0);
}

u8 WORLD_isBlockedCell(s16 u, s16 v)
{
    if (u < 0 || v < 0 || u >= WORLD_W || v >= WORLD_H)
        return 1;
    return world_block[u * WORLD_H + v];
}

/* collision test with player radius (0.2 cell) around target center */
u8 WORLD_isBlocked(s16 u_q8, s16 v_q8)
{
    const s16 r = 51;   /* ~0.2 cells in 8.8 */
    s16 u0 = (u_q8 - r) >> 8;
    s16 v0 = (v_q8 - r) >> 8;
    s16 u1 = (u_q8 + r) >> 8;
    s16 v1 = (v_q8 + r) >> 8;
    s16 du, dv;

    for (du = u0; du <= u1; du++)
        for (dv = v0; dv <= v1; dv++)
            if (WORLD_isBlockedCell(du, dv))
                return 1;
    return 0;
}

s16 worldToScreenX(s16 u_q8, s16 v_q8)
{
    /* (u-v)*16 : careful with signs — do it in 32-bit */
    s32 du = (s32)u_q8 - (s32)v_q8;
    return (s16)((du * 16) >> 8) + PROJ_OFF_X;
}

s16 worldToScreenY(s16 u_q8, s16 v_q8)
{
    s32 sum = (s32)u_q8 + (s32)v_q8;
    return (s16)((sum * 8) >> 8) + PROJ_OFF_Y;
}

void WORLD_posToScreen(const Player *p, s16 *sx, s16 *sy)
{
    *sx = worldToScreenX(p->u_q8, p->v_q8);
    *sy = worldToScreenY(p->u_q8, p->v_q8);
}
