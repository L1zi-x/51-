/******************************************************************************
 * CSGO 下包模拟器
 * 密码 7355608，按S16 确认后进入 40 秒倒计时；
 ******************************************************************************/
#include <REGX52.H>

typedef unsigned char u8;
typedef unsigned int u16;

/* 硬件引脚 */
sbit LSA = P2^2;
sbit LSB = P2^3;
sbit LSC = P2^4;
sbit BEEP = P2^5;

#define KEY_PORT P1
#define BEEP_ON_LEVEL  0
#define BEEP_OFF_LEVEL 1

u8 code SEG_TAB[] =
{
    0x3F, 0x06, 0x5B, 0x4F, 0x66,
    0x6D, 0x7D, 0x07, 0x7F, 0x6F,
    0x00
};
#define SEG_BLANK 10

u8 code POS_CODE[8] = {7, 6, 5, 4, 3, 2, 1, 0};

/* 状态 */
#define ST_INPUT    0   /* 输入密码 */
#define ST_COUNTING 1   /* 40 秒倒计时 */
#define ST_ALARM    2   /* 倒计时结束 */

#define PASSWORD_LEN 7
#define TOTAL_SECONDS 40
u8 code PASSWORD[PASSWORD_LEN] = {7, 3, 5, 5, 6, 0, 8};

volatile u8 gState = ST_INPUT;
u8 gDigits[PASSWORD_LEN];
u8 gDigitCount = 0;
volatile u8 gSeconds;
volatile u16 gMsTick;
volatile u16 gBeepHalfMs;
volatile u16 gBeepTimer;
volatile u8  gToneActive = 0;

/* 延时 */
void DelayMs(u16 ms)
{
    u16 i;
    while (ms--)
        for (i = 0; i < 110; i++);
}

/* 定时器 0：1ms 时基 */
#define TIMER0_RELOAD_H 0xFC
#define TIMER0_RELOAD_L 0x67
void Timer0Init(void)
{
    TMOD &= 0xF0;
    TMOD |= 0x01;   /* Timer0 方式 1 */
    TH0 = TIMER0_RELOAD_H;
    TL0 = TIMER0_RELOAD_L;
    ET0 = 1;
    EA  = 1;
    TR0 = 1;
}

/* 定时器 */
#define TONE_RELOAD_H 0xFF
#define TONE_RELOAD_L 0x1A
void Timer1Init(void)
{
    TMOD &= 0x0F;
    TMOD |= 0x10;   /* Timer1 方式 1 */
    TH1 = TONE_RELOAD_H;
    TL1 = TONE_RELOAD_L;
    ET1 = 1;
    TR1 = 1;
}

void Timer1_ISR(void) interrupt 3
{
    TH1 = TONE_RELOAD_H;
    TL1 = TONE_RELOAD_L;
    if (gToneActive)
        BEEP = !BEEP;
}

void Timer0_ISR(void) interrupt 1
{
    TH0 = TIMER0_RELOAD_H;
    TL0 = TIMER0_RELOAD_L;

    gMsTick++;

    if (gState == ST_COUNTING)
    {
        if (++gBeepTimer >= gBeepHalfMs)
        {
            gBeepTimer = 0;
            gToneActive = !gToneActive;
            if (!gToneActive)
                BEEP = BEEP_OFF_LEVEL;
        }

        if (gMsTick >= 1000)
        {
            gMsTick = 0;
            if (gSeconds > 0)
            {
                gSeconds--;
                gBeepHalfMs = 30 + (u16)gSeconds * 3;

                if (gSeconds == 0)
                {
                    gState = ST_ALARM;
                    gToneActive = 1;
                    BEEP = BEEP_ON_LEVEL;
                }
            }
        }
    }
}

/* 显示 */
void DisplayOne(u8 pos, u8 segIndex)
{
    u8 sel;
    P0 = 0xFF;              /* 消隐 */
    sel = POS_CODE[pos];
    LSA = sel & 0x01;
    LSB = (sel >> 1) & 0x01;
    LSC = (sel >> 2) & 0x01;
    P0 = SEG_TAB[segIndex];
    DelayMs(1);
    P0 = 0x00;
}

void DisplayInput(void)
{
    u8 i;
    u8 start = 8 - gDigitCount;
    for (i = 0; i < 8; i++)
    {
        if (i >= start)
            DisplayOne(i, gDigits[i - start]);
        else
            DisplayOne(i, SEG_BLANK);
    }
}

/* 倒计时 */
void DisplayCountdown(void)
{
    if (gSeconds >= 10)
        DisplayOne(6, gSeconds / 10);
    else
        DisplayOne(6, SEG_BLANK);
    DisplayOne(7, gSeconds % 10);
}

/* 报警 */
void DisplayAlarm(void)
{
    DisplayOne(6, SEG_BLANK);
    DisplayOne(7, 0);
}

/* 矩阵键盘 */
u8 KeyScan(void)
{
    u8 key = 0;

    KEY_PORT = 0x0F;    /* 列扫描 */
    if (KEY_PORT != 0x0F)
    {
        DelayMs(10);    /* 消抖 */
        if (KEY_PORT != 0x0F)
        {
            switch (KEY_PORT)
            {
                case 0x07: key = 1;  break;
                case 0x0B: key = 2;  break;
                case 0x0D: key = 3;  break;
                case 0x0E: key = 4;  break;
                default:   key = 0;  break;
            }

            KEY_PORT = 0xF0;    /* 行扫描 */
            switch (KEY_PORT & 0xF0)
            {
                case 0x70: key += 0;  break;
                case 0xB0: key += 4;  break;
                case 0xD0: key += 8;  break;
                case 0xE0: key += 12; break;
                default:   key = 0;   break;
            }

            while (KEY_PORT != 0xF0);   /* 等待松开 */
        }
    }
    return key;
}

/* 清空 */
void ClearInput(void)
{
    u8 i;
    for (i = 0; i < PASSWORD_LEN; i++)
        gDigits[i] = 0;
    gDigitCount = 0;
}

/* 错误提示 */
void ErrorBeep(void)
{
    u8 i;
    for (i = 0; i < 2; i++)
    {
        gToneActive = 1;
        BEEP = BEEP_ON_LEVEL;
        DelayMs(120);
        gToneActive = 0;
        BEEP = BEEP_OFF_LEVEL;
        DelayMs(80);
    }
}

/* 确认密码 */
void ConfirmPassword(void)
{
    u8 i;
    u8 ok = 1;

    if (gDigitCount != PASSWORD_LEN)
        ok = 0;
    else
    {
        for (i = 0; i < PASSWORD_LEN; i++)
        {
            if (gDigits[i] != PASSWORD[i])
            {
                ok = 0;
                break;
            }
        }
    }

    if (ok)
    {
        gState = ST_COUNTING;
        gSeconds = TOTAL_SECONDS;
        gMsTick = 0;
        gBeepTimer = 0;
        gBeepHalfMs = 30 + (u16)gSeconds * 3;
        gToneActive = 1;
        BEEP = BEEP_OFF_LEVEL;
    }
    else
    {
        ClearInput();
        ErrorBeep();
    }
}

/* 复位 */
void ResetSystem(void)
{
    ClearInput();
    gState = ST_INPUT;
    gSeconds = 0;
    gMsTick = 0;
    gBeepTimer = 0;
    gToneActive = 0;
    BEEP = BEEP_OFF_LEVEL;
}

void main(void)
{
    u8 key;

    Timer0Init();
    Timer1Init();
    ClearInput();

    while (1)
    {
        key = KeyScan();

        if (key != 0)
        {
            if (gState == ST_INPUT)
            {
                if (key == 14)
                    ClearInput();
                else if (key == 16)
                    ConfirmPassword();
                else if (key >= 1 && key <= 10)
                {
                    if (gDigitCount < PASSWORD_LEN)
                    {
                        gDigits[gDigitCount] = (key == 10) ? 0 : key;
                        gDigitCount++;
                    }
                    else
                        ErrorBeep();
                }
            }
            else if (gState == ST_COUNTING || gState == ST_ALARM)
            {
                if (key == 14)
                    ResetSystem();
            }
        }

        if (gState == ST_INPUT)
            DisplayInput();
        else if (gState == ST_COUNTING)
            DisplayCountdown();
        else
            DisplayAlarm();
    }
}
