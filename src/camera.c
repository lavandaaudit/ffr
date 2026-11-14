/*
 * camera.c — smooth follow camera, clamped to map bounds.
 */
#include "game.h"

Camera camera = { 0, 0, 0, 0 };

#define CAM_MARGIN_X  160
#define CAM_MARGIN_Y  112

static s16 clampCamX(s32 x)
{
    s32 max = MAP_IMG_W - 320;
    if (x < 0) x = 0;
    if (x > max) x = max;
    return (s16)x;
}

static s16 clampCamY(s32 y)
{
    s32 max = MAP_IMG_H - 224;
    if (y < 0) y = 0;
    if (y > max) y = max;
    return (s16)y;
}

void CAM_update(void)
{
    s16 px, py;
    WORLD_posToScreen(&player, &px, &py);
    camera.tx = clampCamX(px - CAM_MARGIN_X);
    camera.ty = clampCamY(py - CAM_MARGIN_Y);

    /* smooth follow */
    camera.x += (camera.tx - camera.x) >> 3;
    camera.y += (camera.ty - camera.y) >> 3;

    MAP_scrollTo(worldMapGet(), camera.x, camera.y);
}

void CAM_snap(void)
{
    s16 px, py;
    WORLD_posToScreen(&player, &px, &py);
    camera.x = clampCamX(px - CAM_MARGIN_X);
    camera.y = clampCamY(py - CAM_MARGIN_Y);
    camera.tx = camera.x;
    camera.ty = camera.y;
    MAP_scrollTo(worldMapGet(), camera.x, camera.y);
}
