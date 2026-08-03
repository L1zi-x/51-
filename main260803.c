#include <reg52.h>

typedef unsigned int  u16;
typedef unsigned char u8;

sbit LSA = P2^2;
sbit LSB = P2^3;
sbit LSC = P2^4;

u8 code smg_table[] = {
    0x3F, // 0
    0x06, // 1
    0x5B, // 2
    0x4F, // 3
    0x66, // 4
    0x6D, // 5
    0x7D, // 6
    0x07, // 7
    0x7F, // 8
    0x6F  // 9
};

//延时函数
void delay_ms(u16 ms)
{
    u16 i, j;
    for(i = ms; i > 0; i--)
        for(j = 114; j > 0; j--);
}

void main(void)
{
    u8 num;
    //最左侧为    LSC = 0;LSB = 0;LSA = 0;
    //更换为最右侧的话改成为LSC=1; LSB=1; LSA=1;
    LSC = 1;
    LSB = 1;
    LSA = 1;
    
    while(1)
    {        // 从0到9依次遍历
        for(num = 0; num <= 9; num++)
        {
            P0 = smg_table[num];  
            delay_ms(500);  
					
            P0 = 0x00;
            delay_ms(200);        
        }
    }
}