/*
 * items.c — item definitions.
 */
#include "game.h"

/* PLACEHOLDER names/descriptions are final Ukrainian text; item icons are
 * generated placeholder art (see res/items.png). */

const ItemDef itemDefs[ITEM_COUNT] = {
    /* I_KNIFE */
    { "КУХОННИЙ НІЖ", "ЗВИЧАЙНИЙ НІЖ. ТЕПЕР — ЗАХИСТ.",
      IC_WEAPON, 1 },
    /* I_WATER */
    { "ВОДА", "ЧИСТА ВОДА. ВГАМОВУЄ СПАГУ.",
      IC_MISC, 3 },
    /* I_MEDKIT */
    { "АПТЕЧКА", "ДОПОМОГА. ВІДНОВЛЮЄ ЗДОРОВ'Я І ТРАВМИ.",
      IC_MED, 2 },
    /* I_BANDAGE */
    { "БИНТ", "ЗУПИНЯЄ КРОВ. ЗМЕНШУЄ ТРАВМУ.",
      IC_MED, 3 },
    /* I_PARTS */
    { "МЕТАЛЕВІ ДЕТАЛІ", "ЗАПЧАСТИНИ. ЗНАДОБЛЯТЬСЯ ДЛЯ РЕМОНТУ.",
      IC_MISC, 5 },
    /* I_BATTERY */
    { "БАТАРЕЙКИ", "ЖИВЛЕННЯ ДЛЯ РАДІОПРИЙМАЧІВ.",
      IC_MISC, 5 },
};
