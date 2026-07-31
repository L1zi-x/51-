#include <REGX52.H>

typedef unsigned char u8;
typedef unsigned int u16;
sbit LSA = P2^2;
sbit LSB = P2^3;
sbit LSC = P2^4;
sbit BEEP = P2^5;

#define KEY_PORT P1

#define BEEP_ON_LEVEL  0
#define BEEP_OFF_LEVEL 1

#define PASSWORD_LEN 7

u8 code PASSWORD[PASSWORD_LEN] = {7, 3, 5, 5, 6, 0, 8};

u8 code SEG_TAB[] =
{
    0x3F, 0x06, 0x5B, 0x4F, 0x66,
    0x6D, 0x7D, 0x07, 0x7F, 0x6F,
    0x00
};
#define SEG_BLANK 10

u8 code POS_CODE[8] = {7, 6, 5, 4, 3, 2, 1, 0};

u8 gDigits[PASSWORD_LEN];
u8 gDigitCount = 0;
volatile u8 gToneActive = 0;
extern void StartCountdown(void);

void DelayMs(u16 ms)
{
    u16 i;
    while (ms--)
        for (i = 0; i < 110; i++);
}

void DisplayOne(u8 pos, u8 segIndex)
{
    u8 sel;
    P0 = 0xFF;              
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

u8 KeyScan(void)
{
    u8 key = 0;

    KEY_PORT = 0x0F;   
    if (KEY_PORT != 0x0F)
    {
        DelayMs(10);   
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

            KEY_PORT = 0xF0;  
            switch (KEY_PORT & 0xF0)
            {
                case 0x70: key += 0;  break;
                case 0xB0: key += 4;  break;
                case 0xD0: key += 8;  break;
                case 0xE0: key += 12; break;
                default:   key = 0;   break;
            }

            while (KEY_PORT != 0xF0);   
        }
    }
    return key;
}

void ClearInput(void)
{
    u8 i;
    for (i = 0; i < PASSWORD_LEN; i++)
        gDigits[i] = 0;
    gDigitCount = 0;
}

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
        StartCountdown();
    else
    {
        ClearInput();
        ErrorBeep();
    }
}
