/*
 * ui.c — Ukrainian text engine (custom glyph tiles), panels, menus.
 *
 * The SGDK default font has no Cyrillic glyphs, so we load our own
 * generated 8x8 glyph tilesets (res/font_*.png) into VRAM and write
 * plane tiles directly with VDP_setTileMapXY. UTF-8 strings in the
 * source are decoded at render time.
 *
 * Font VRAM layout: FONT_BASE + color*FONT_SLOT + glyphIndex
 */
#include "game.h"
#include "resources.h"

#define GLYPH_SPACE 0xFF

/* charcode -> glyph index map is generated to match gen_sprites.py CHARSET */
#include "charset.h"

static u16 fontBaseTile[4];

void UI_init(void)
{
    fontBaseTile[FC_WHITE] = FONT_BASE + FONT_SLOT * 0;
    fontBaseTile[FC_BLUE]  = FONT_BASE + FONT_SLOT * 1;
    fontBaseTile[FC_RED]   = FONT_BASE + FONT_SLOT * 2;
    fontBaseTile[FC_CYAN]  = FONT_BASE + FONT_SLOT * 3;

    /* load glyph tilesets (each is a TILESET resource, 119 tiles) */
    VDP_loadTileSet(&ts_font_white, fontBaseTile[FC_WHITE], CPU);
    VDP_loadTileSet(&ts_font_blue,  fontBaseTile[FC_BLUE], CPU);
    VDP_loadTileSet(&ts_font_red,   fontBaseTile[FC_RED], CPU);
    VDP_loadTileSet(&ts_font_cyan,  fontBaseTile[FC_CYAN], CPU);

    /* hud bar/box tiles */
    VDP_loadTileSet(&ts_hud, HUD_TILES_BASE, CPU);
}

/* utf8 decode: returns glyph index, advances *p; space (or unknown) maps to blank */
static u8 ui_nextGlyph(const char **p)
{
    const char *s = *p;
    u8 c = (u8)*s;
    if (c == 0)
        return GLYPH_SPACE;
    if (c < 0x80)
    {
        *p = s + 1;
        /* ASCII direct */
        return charMap[c];
    }
    else if ((c & 0xE0) == 0xC0)
    {
        u8 c2 = (u8)s[1];
        *p = s + 2;
        u16 code = ((u16)(c & 0x1F) << 6) | (c2 & 0x3F);
        return charMapLookup(code);
    }
    else if ((c & 0xF0) == 0xE0)
    {
        u8 c2 = (u8)s[1];
        u8 c3 = (u8)s[2];
        *p = s + 3;
        u16 code = ((u16)(c & 0x0F) << 12) | ((u16)(c2 & 0x3F) << 6) | (c3 & 0x3F);
        return charMapLookup(code);
    }
    /* invalid */
    *p = s + 1;
    return GLYPH_SPACE;
}

u16 UI_charTile(u16 color, u8 glyph)
{
    u16 base = fontBaseTile[color & 3];
    if (glyph == GLYPH_SPACE)
        return 0;   /* tile 0 = transparent on plane A */
    return TILE_ATTR_FULL(PAL0, TRUE, FALSE, FALSE, base + glyph);
}

void UI_textLen(u16 color, const char *s, u16 x, u16 y, u16 maxlen)
{
    u16 cx = x;
    const char *p = s;
    while (*p && (cx - x) < maxlen)
    {
        u8 g = ui_nextGlyph(&p);
        VDP_setTileMapXY(BG_A, UI_charTile(color, g), cx, y);
        cx++;
    }
}

void UI_text(u16 color, const char *s, u16 x, u16 y)
{
    UI_textLen(color, s, x, y, 400);
}

void UI_clearRect(u16 x, u16 y, u16 w, u16 h)
{
    u16 xx, yy;
    for (yy = y; yy < y + h; yy++)
        for (xx = x; xx < x + w; xx++)
            VDP_setTileMapXY(BG_A, 0, xx, yy);
}

/* hud tile indices inside ts_hud (order from gen_sprites.py):
 * 0..29  bar tiles: 5 colors x (fullL fullM fullR emptyL emptyM emptyR)
 *        colors: red=0 blue=6 yel=12 cyan=18 org=24
 * 30..38 panel box (tl t tr l m r bl b br)
 * 39 cursor
 * 40 blank
 */
#define HUDT(c, i)   (HUD_TILES_BASE + (c) * 6 + (i))
#define BOXT(i)      (HUD_TILES_BASE + 30 + (i))

static u16 barTileAttr(u16 idx)
{
    return TILE_ATTR_FULL(PAL0, TRUE, FALSE, FALSE, idx);
}

void UI_box(u16 x, u16 y, u16 w, u16 h)
{
    u16 xx, yy;
    if (w < 2 || h < 2) return;
    /* top row */
    VDP_setTileMapXY(BG_A, barTileAttr(BOXT(0)), x, y);
    for (xx = 1; xx < w - 1; xx++)
        VDP_setTileMapXY(BG_A, barTileAttr(BOXT(1)), x + xx, y);
    VDP_setTileMapXY(BG_A, barTileAttr(BOXT(2)), x + w - 1, y);
    /* middle rows */
    for (yy = 1; yy < h - 1; yy++)
    {
        VDP_setTileMapXY(BG_A, barTileAttr(BOXT(3)), x, y + yy);
        for (xx = 1; xx < w - 1; xx++)
            VDP_setTileMapXY(BG_A, barTileAttr(BOXT(4)), x + xx, y + yy);
        VDP_setTileMapXY(BG_A, barTileAttr(BOXT(5)), x + w - 1, y + yy);
    }
    /* bottom row */
    VDP_setTileMapXY(BG_A, barTileAttr(BOXT(6)), x, y + h - 1);
    for (xx = 1; xx < w - 1; xx++)
        VDP_setTileMapXY(BG_A, barTileAttr(BOXT(7)), x + xx, y + h - 1);
    VDP_setTileMapXY(BG_A, barTileAttr(BOXT(8)), x + w - 1, y + h - 1);
}

/* bar drawing: width w tiles, value 0..100, color = bar color set index 0..4 */
void UI_bar(u16 x, u16 y, u16 w, s16 value, u16 colorSet)
{
    s16 fill = (value * (w - 2) + 50) / 100;    /* inner tiles */
    u16 i;
    if (fill < 0) fill = 0;
    if (fill > w - 2) fill = w - 2;
    /* left cap */
    VDP_setTileMapXY(BG_A, barTileAttr(HUDT(colorSet, fill > 0 ? 0 : 3)), x, y);
    for (i = 1; i < w - 1; i++)
    {
        u16 t;
        if (i <= (u16)fill)
            t = HUDT(colorSet, (i == (u16)fill) ? 2 : 1);
        else
            t = HUDT(colorSet, (i == w - 2) ? 5 : 4);
        VDP_setTileMapXY(BG_A, barTileAttr(t), x + i, y);
    }
    VDP_setTileMapXY(BG_A, barTileAttr(HUDT(colorSet, 2)), x + w - 1, y);
}
