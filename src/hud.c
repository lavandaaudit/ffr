/*
 * hud.c — IBONARIUM DARK HUD: bars, task line, messages, interact prompt.
 * Drawn on BG_A (static plane above the scrolling map).
 */
#include "game.h"

#define BAR_HP   0
#define BAR_STA  1
#define BAR_GOL  2
#define BAR_SPA  3
#define BAR_TRAV 4

static const char *hudTaskText = NULL;
static u16 taskFlash = 0;
static char msgLine1[44];
static char msgLine2[44];
static s16 msgTimer = 0;
static char promptText[28];
static bool promptVisible = FALSE;
static char taskBuf[40];

/* small string copy (SGDK strcpy is available but keep own for safety) */
static void scopy(char *dst, const char *src, u16 max)
{
    u16 i = 0;
    while (src[i] && i < max - 1)
    {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

void HUD_init(void)
{
    hudTaskText = NULL;
    msgTimer = 0;
    promptVisible = FALSE;
    msgLine1[0] = 0;
    msgLine2[0] = 0;
    promptText[0] = 0;
}

void HUD_clearAll(void)
{
    UI_clearRect(0, 0, 40, 3);
    UI_clearRect(0, 22, 40, 3);
    hudTaskText = NULL;
}

void HUD_setTask(const char *text)
{
    hudTaskText = text;
    taskFlash = 90;
    if (text != NULL)
    {
        scopy(taskBuf, text, 40);
    }
}

void HUD_flashTaskUpdate(void)
{
    taskFlash = 90;
}

static void drawTaskLine(void)
{
    UI_clearRect(0, 2, 40, 1);
    if (hudTaskText != NULL)
    {
        if ((taskFlash & 16) || taskFlash == 0)
        {
            if (taskFlash > 0)
                UI_text(FC_WHITE, "ЗАВДАННЯ ОНОВЛЕНО", 24, 2);
            else
            {
                UI_text(FC_BLUE, "ЗАВДАННЯ:", 1, 2);
                UI_text(FC_WHITE, taskBuf, 11, 2);
            }
        }
    }
    if (taskFlash > 0)
        taskFlash--;
}

void HUD_drawBars(void)
{
    /* row 0: HP + stamina */
    UI_text(FC_RED, "ЖИТ", 1, 0);
    UI_bar(5, 0, 8, player.hp, BAR_HP);
    UI_text(FC_CYAN, "ВИН", 15, 0);
    UI_bar(19, 0, 8, player.sta, BAR_STA);
    /* row 1: hunger / thirst / injury */
    UI_text(FC_WHITE, "ГОЛ", 1, 1);
    UI_bar(5, 1, 5, player.hunger, BAR_GOL);
    UI_text(FC_WHITE, "СПА", 11, 1);
    UI_bar(15, 1, 5, player.thirst, BAR_SPA);
    UI_text(FC_WHITE, "ТРАВ", 21, 1);
    UI_bar(26, 1, 5, player.injury, BAR_TRAV);
    /* warnings */
    if (player.thirst < 15 && (frameCount & 32))
        UI_text(FC_RED, "СПАГА!", 33, 1);
    else if (player.hunger < 15 && (frameCount & 32))
        UI_text(FC_RED, "ГОЛОД!", 33, 1);
    else
        UI_clearRect(33, 1, 7, 1);
    if (player.hp < 30 && (frameCount & 16))
        UI_text(FC_RED, "КРИТИЧНЕ ЗДОРОВ'Я", 22, 0);
    else if (player.hp < 30)
        UI_clearRect(22, 0, 18, 1);

    drawTaskLine();
}

void HUD_message(const char *line1, const char *line2)
{
    if (line1 != NULL)
        scopy(msgLine1, line1, 44);
    else
        msgLine1[0] = 0;
    if (line2 != NULL)
        scopy(msgLine2, line2, 44);
    else
        msgLine2[0] = 0;
    msgTimer = 240;
    /* draw immediately */
    UI_clearRect(0, 23, 40, 2);
    if (msgLine1[0]) UI_text(FC_WHITE, msgLine1, 1, 23);
    if (msgLine2[0]) UI_text(FC_WHITE, msgLine2, 1, 24);
}

void HUD_updateMessages(void)
{
    if (msgTimer > 0)
    {
        msgTimer--;
        if (msgTimer == 60)
        {
            /* blink before fade */
        }
        if (msgTimer == 0)
        {
            UI_clearRect(0, 23, 40, 2);
            msgLine1[0] = 0;
            msgLine2[0] = 0;
        }
        else if (msgTimer < 60 && (msgTimer & 8))
        {
            UI_clearRect(0, 23, 40, 2);
        }
        else if (msgTimer < 60)
        {
            if (msgLine1[0]) UI_text(FC_WHITE, msgLine1, 1, 23);
            if (msgLine2[0]) UI_text(FC_WHITE, msgLine2, 1, 24);
        }
    }
}

void HUD_setPrompt(const char *text)
{
    if (text == NULL)
    {
        if (promptVisible)
        {
            UI_clearRect(0, 22, 40, 1);
            promptVisible = FALSE;
        }
        promptText[0] = 0;
        return;
    }
    scopy(promptText, text, 28);
    if (!promptVisible || TRUE)
    {
        UI_clearRect(0, 22, 40, 1);
        UI_text(FC_CYAN, promptText, 1, 22);
        promptVisible = TRUE;
    }
}

void HUD_update(void)
{
    HUD_drawBars();
    HUD_updateMessages();
}
