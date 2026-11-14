/*
 * inventory.c — 12 slot inventory + weapon slot handling.
 */
#include "game.h"

void INV_init(Inventory *inv)
{
    u16 i;
    for (i = 0; i < INV_SLOTS; i++)
    {
        inv->item[i] = 0xFF;
        inv->count[i] = 0;
    }
    inv->weapon = -1;
}

s8 INV_add(Inventory *inv, ItemType it, u8 count)
{
    s8 i;
    const ItemDef *def = &itemDefs[it];

    if (def->category == IC_WEAPON && inv->weapon >= 0)
        return -1;      /* already have a weapon */

    /* stack into existing slot */
    if (def->maxStack > 1)
    {
        for (i = 0; i < INV_SLOTS; i++)
        {
            if (inv->item[i] == (u8)it && inv->count[i] < def->maxStack)
            {
                s16 space = def->maxStack - inv->count[i];
                s16 add = count < space ? count : space;
                inv->count[i] += add;
                count -= add;
                if (count == 0)
                    return 1;
            }
        }
    }
    /* new slots */
    for (i = 0; i < INV_SLOTS; i++)
    {
        if (inv->item[i] == 0xFF)
        {
            inv->item[i] = (u8)it;
            inv->count[i] = count > def->maxStack ? def->maxStack : count;
            count -= inv->count[i];
            if (def->category == IC_WEAPON && inv->weapon < 0)
                inv->weapon = i;
            if (count == 0)
                return 1;
            /* remaining spills into next loop iterations */
        }
    }
    if (count > 0)
        return -1;      /* inventory full */
    return 1;
}

u8 INV_count(Inventory *inv, ItemType it)
{
    u8 i, n = 0;
    for (i = 0; i < INV_SLOTS; i++)
        if (inv->item[i] == (u8)it)
            n += inv->count[i];
    return n;
}

bool INV_useSlot(Inventory *inv, u8 slot)
{
    const ItemDef *def;
    if (slot >= INV_SLOTS || inv->item[slot] == 0xFF)
        return FALSE;

    def = &itemDefs[inv->item[slot]];

    switch (inv->item[slot])
    {
    case I_WATER:
        player.thirst += 40;
        if (player.thirst > 100) player.thirst = 100;
        SFX_play(SFX_USE);
        HUD_message("ВИ ПИЄШ ВОДУ. СПАГА ВГАМОВАНА.", NULL);
        break;
    case I_MEDKIT:
        player.hp += 50;
        if (player.hp > 100) player.hp = 100;
        player.injury = 0;
        SFX_play(SFX_USE);
        HUD_message("АПТЕЧКА ВИКОРИСТАНА. ЛЕГШЕ.", NULL);
        break;
    case I_BANDAGE:
        player.injury -= 40;
        if (player.injury < 0) player.injury = 0;
        SFX_play(SFX_USE);
        HUD_message("ПОВ'ЯЗКА НАКЛАДЕНА.", NULL);
        break;
    default:
        return FALSE;   /* not consumable */
    }

    inv->count[slot]--;
    if (inv->count[slot] == 0)
    {
        if (inv->weapon == (s8)slot)
            inv->weapon = -1;
        inv->item[slot] = 0xFF;
    }
    return TRUE;
}
