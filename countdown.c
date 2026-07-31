#include <REGX52.H>

typedef unsigned char u8;
typedef unsigned int u16;

sbit LSA = P2^2;
sbit LSB = P2^3;
sbit LSC = P2^4;
sbit BEEP = P2^5;

#define BEEP_ON_LEVEL  0
#define BEEP_OFF_LEVEL 1

#define ST_INPUT    0
#define ST_COUNTING 1
#define ST_ALARM    2

#define TOTAL_SECONDS 40

volatile u8 gState = ST_INPUT;
volatile u8 gSeconds;
volatile u16 gMsTick;
volatile u16 gBeepHalfMs;
volatile u16 gBeepTimer;
volatile u8  gToneActive = 0;
extern void ClearInput(void);

void DelayMs(u16 ms)
{
    u16 i;
    while (ms--)
        for (i = 0; i < 110; i++);
}

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

u8 code SEG_TAB[] =
{
    0x3F, 0x06, 0x5B, 0x4F, 0x66,
    0x6D, 0x7D, 0x07, 0x7F, 0x6F,
    0x00
};
#define SEG_BLANK 10

u8 code POS_CODE[8] = {7, 6, 5, 4, 3, 2, 1, 0};

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

void DisplayCountdown(void)
{
    if (gSeconds >= 10)
        DisplayOne(6, gSeconds / 10);
    else
        DisplayOne(6, SEG_BLANK);
    DisplayOne(7, gSeconds % 10);
}

void DisplayAlarm(void)
{
    DisplayOne(6, SEG_BLANK);
    DisplayOne(7, 0);
}

void StartCountdown(void)
{
    gState = ST_COUNTING;
    gSeconds = TOTAL_SECONDS;
    gMsTick = 0;
    gBeepTimer = 0;
    gBeepHalfMs = 30 + (u16)gSeconds * 3;
    gToneActive = 1;
    BEEP = BEEP_OFF_LEVEL;
}

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
