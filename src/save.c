/*
 * save.c — SRAM save/load with checksum.
 * The ROM header declares SRAM at 0x200000..0x20FFFF ("RA" 0xF820).
 * Data is stored at offset 0, byte-wise via SGDK SRAM API.
 */
#include "game.h"

#define SAVE_MAGIC   0x49424F4EL  /* 'IBON' */
#define SAVE_VERSION 0x0001

u16 SAVE_computeChecksum(const SaveData *d)
{
    const u8 *p = (const u8 *)d;
    u32 n = sizeof(SaveData) - 2;   /* exclude checksum field itself */
    u16 sum = 0x4A4F;
    u32 i;
    for (i = 0; i < n; i++)
        sum = (sum << 1) + sum + p[i];
    return sum;
}

bool SAVE_exists(void)
{
    u32 m;
    SRAM_enableRO();
    m = ((u32)SRAM_readByte(0) << 24) | ((u32)SRAM_readByte(1) << 16) |
        ((u32)SRAM_readByte(2) << 8) | (u32)SRAM_readByte(3);
    SRAM_disable();
    return m == SAVE_MAGIC;
}

bool SAVE_load(SaveData *out)
{
    u8 *p = (u8 *)out;
    u32 i;
    u16 sum;

    SRAM_enableRO();
    for (i = 0; i < sizeof(SaveData); i++)
        p[i] = SRAM_readByte(i);
    SRAM_disable();

    if (out->magic != SAVE_MAGIC || out->version != SAVE_VERSION)
        return FALSE;
    sum = SAVE_computeChecksum(out);
    return sum == out->checksum;
}

void SAVE_write(const SaveData *data)
{
    const u8 *p = (const u8 *)data;
    u32 i;
    SaveData copy = *data;
    copy.magic = SAVE_MAGIC;
    copy.version = SAVE_VERSION;
    copy.checksum = SAVE_computeChecksum(&copy);
    p = (const u8 *)&copy;

    SRAM_enable();
    for (i = 0; i < sizeof(SaveData); i++)
        SRAM_writeByte(i, p[i]);
    SRAM_disable();
}
