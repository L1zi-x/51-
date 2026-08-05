//=====================================================================
// 三角洲密码解锁小游戏（普中51开发板适用）
// 功能键：8号键复位，12号键退格，16号键确认进入验证
// 游戏逻辑：
// 阶段1：设置5位数字密码，数字键输入
// 阶段2：数码管数字自动滚动，按16确认当前数字匹配密码位，正确短鸣、错误长短交替蜂鸣
// 正确的密码会固定显示，下一位开始重复滚动
// 阶段3：5位全部匹配成功，完整播放通关旋律，数码管显示完整密码
// 详情请看README,以下注释均由AI编写，说实话有些AI写的我自己也看不懂
//=====================================================================
#include <REG52.H>

typedef unsigned char u8;
typedef unsigned int  u16;
typedef unsigned long u32;

sbit LSA  = P2^2;
sbit LSB  = P2^3;
sbit LSC  = P2^4;
sbit BEEP = P2^5;

#define PASSWORD_LEN       5       // 密码固定5位长度
#define DIGIT_COUNT        8       // 开发板数码管总个数8个
#define KEY_DEBOUNCE_MS    15      // 按键消抖稳定时长15ms

#define T0_RELOAD          (65536U - 922U)
#define T0_RELOAD_H        ((65536U - 922U) >> 8) 
#define T0_RELOAD_L        ((65536U - 922U) & 0xFF)

#define DIGIT_ORDER_REVERSED 1 

// 系统运行状态枚举
typedef enum {
    STATE_SET_PASSWORD = 0,  // 状态0：设置密码界面
    STATE_VERIFY_PASSWORD,   // 状态1：密码验证游戏界面
    STATE_SUCCESS           // 状态2：密码验证成功通关界面
} SystemState;

// 蜂鸣器工作模式枚举
typedef enum {
    BEEP_IDLE = 0,    // 模式0：蜂鸣器空闲静音
    BEEP_ONCE,        // 模式1：单次短促提示音
    BEEP_ERROR,       // 模式2：密码错误警示音
    BEEP_MELODY       // 模式3：通关完整旋律播放
} BeepMode;

// 全局中断变量，volatile防止编译器优化
volatile u32 g_ms = 0;               // 系统总毫秒计时
volatile u8 g_scan_pos = 0;          // 数码管动态扫描当前位索引
volatile bit g_beep_enable = 0;      // 蜂鸣器发声使能标志
volatile u16 g_t1_reload = 0;        // 定时器1音调初值缓存
volatile BeepMode g_beep_mode = BEEP_IDLE; // 当前蜂鸣工作模式
volatile u16 g_beep_elapsed = 0;     // 蜂鸣器当前发声计时
volatile u16 g_beep_total = 0;       // 当前蜂鸣模式总持续时长
volatile bit g_beep_done = 0;        // 蜂鸣播放完成标志位
volatile u8 g_melody_index = 0;      // 旋律播放音符索引

// 游戏业务全局变量
SystemState g_state = STATE_SET_PASSWORD; // 系统默认初始状态：设置密码
u8 g_password[PASSWORD_LEN] = {0};        // 存储用户设置的5位密码数组
u8 g_input_len = 0;                       // 设置密码时已输入位数计数
u8 g_verify_index = 0;                    // 验证密码当前匹配到第几位
u8 g_roll_num = 1;                        // 验证界面自动滚动的数字
u16 g_roll_elapsed = 0;                   // 数字滚动毫秒计时器
bit g_wait_beep_result = 0;               // 等待蜂鸣播放完成再切换下一位标志
bit g_pending_correct = 0;                // 暂存当前密码位是否匹配正确

#define SEG_COMMON_ANODE 0  // 数码管类型选择：0=共阴极 1=共阳极
#if SEG_COMMON_ANODE
// 共阳极数码管段码，熄灭码0xFF
#define SEG_BLANK 0xFF
code u8 SEG_CODE[10] = {
    0xC0, // 数字0
    0xF9, // 数字1
    0xA4, // 数字2
    0xB0, // 数字3
    0x99, // 数字4
    0x92, // 数字5
    0x82, // 数字6
    0xF8, // 数字7
    0x80, // 数字8
    0x90  // 数字9
};
#else
// 共阴极数码管段码，熄灭码0x00
#define SEG_BLANK 0x00
code u8 SEG_CODE[10] = {
    0x3F, // 数字0
    0x06, // 数字1
    0x5B, // 数字2
    0x4F, // 数字3
    0x66, // 数字4
    0x6D, // 数字5
    0x7D, // 数字6
    0x07, // 数字7
    0x7F, // 数字8
    0x6F  // 数字9
};
#endif

volatile u8 g_disp[DIGIT_COUNT] = { // 8位数码管显示缓存数组，上电全熄灭
    SEG_BLANK, SEG_BLANK, SEG_BLANK, SEG_BLANK,
    SEG_BLANK, SEG_BLANK, SEG_BLANK, SEG_BLANK
};

// 蜂鸣器旋律音符频率宏定义，单位Hz
#define NOTE_REST 0     // 休止符，不发声
#define NOTE_G4   392
#define NOTE_C5   523
#define NOTE_E5   659
#define NOTE_G5   784
#define NOTE_C6   1047
#define NOTE_E6   1319
#define NOTE_G6   1568
#define NOTE_E6B  1319
#define NOTE_GS4  415
#define NOTE_AS4  466
#define NOTE_CS5  554
#define NOTE_DS5  622
#define NOTE_GS5  831

// 通关旋律音符频率表，code存程序Flash节省RAM
code u16 MELODY_FREQ[] = {
    NOTE_G4, NOTE_C5, NOTE_E5, NOTE_G5, NOTE_C6, NOTE_E6, NOTE_G6,
    NOTE_E6B, NOTE_GS4, NOTE_C5, NOTE_DS5, NOTE_GS5, NOTE_C6,
    NOTE_REST
};
// 对应每个音符持续时长，单位ms，末尾0代表旋律结束
code u16 MELODY_DUR[] = {
    90, 90, 90, 90, 90, 90, 260,
    120, 90, 90, 90, 90, 300,
    0
};

// 函数前置声明
static void Display_Clear(void);         // 清空所有数码管显示
static void System_Reset(void);           // 系统全局复位
static void State_SetPassword_Init(void); // 初始化设置密码界面参数
static void State_Verify_Init(void);     // 初始化密码验证游戏界面参数
static void State_Success_Init(void);     // 初始化验证成功通关界面
static void Beep_Stop(void);              // 停止蜂鸣器所有发声
static void Beep_StartOnce(u16 freq, u16 duration_ms); // 播放单次提示音
static void Beep_StartError(void);        // 播放密码错误提示音
static void Beep_StartMelody(void);       // 播放完整通关旋律

// 根据发声频率计算定时器1自动重装初值
static u16 Timer1ReloadByFreq(u16 freq)
{
    u32 half_counts;
    if (freq == 0) { // 频率0代表休止符，无初值
        return 0;
    }
    // 11.0592MHz系统时钟，计算半个声波周期需要计数次数
    half_counts = 921600UL / ((u32)freq * 2UL);
    // 65536减去半周期计数，得到定时器重装值
    return (u16)(65536UL - half_counts);
}

// 软件毫秒延时函数，用于按键消抖短暂等待
static void DelayMsSoft(u8 ms)
{
    u8 i, j;
    while (ms--) {
        for (i = 0; i < 12; i++) {
            for (j = 0; j < 169; j++);
        }
    }
}

// 定时器初始化函数：T0产生1ms时基，T1控制蜂鸣器音调
static void Timer_Init(void)
{
    TMOD &= 0x00;
    TMOD |= 0x11;          // 设置T0、T1均为工作模式1（16位定时器）
    // 配置定时器0
    TH0 = T0_RELOAD_H;
    TL0 = T0_RELOAD_L;
    ET0 = 1;               // 使能定时器0中断
    TR0 = 1;               // 开启定时器0计数
    // 配置定时器1初始状态
    TH1 = 0;
    TL1 = 0;
    ET1 = 1;               // 使能定时器1中断
    TR1 = 0;               // 默认关闭定时器1（蜂鸣器静音）
    EA = 1;                // 开启总中断允许
}

// 74HC138译码选中指定位置数码管，pos为逻辑显示位置
static void Digit_Select(u8 pos)
{
#if DIGIT_ORDER_REVERSED
    pos = 7 - pos; // 位序反转，适配开发板硬件左右颠倒
#endif
    // 拆分3位地址送入译码器引脚
    LSA = pos & 0x01;
    LSB = (pos >> 1) & 0x01;
    LSC = (pos >> 2) & 0x01;
}

// 指定位置数码管显示单个数字0~9
static void Display_SetDigit(u8 pos, u8 num)
{
    if (pos < DIGIT_COUNT && num <= 9) { // 边界校验防止越界
        g_disp[pos] = SEG_CODE[num];
    }
}

// 熄灭指定位置数码管
static void Display_BlankDigit(u8 pos)
{
    if (pos < DIGIT_COUNT) {
        g_disp[pos] = SEG_BLANK;
    }
}

// 清空全部8个数码管显示缓存
static void Display_Clear(void)
{
    u8 i;
    for (i = 0; i < DIGIT_COUNT; i++) {
        g_disp[i] = SEG_BLANK;
    }
}

// 底层4*4矩阵键盘扫描，返回原始按键编号1~16，无按键返回0
static u8 Key_ReadRaw(void)
{
    u8 key = 0;
    // 第一行扫描：P1.4输出低电平，读取P1高四位判断列
    P1 = 0xF7;
    if (P1 != 0xF7) {
        DelayMsSoft(1); // 短暂延时简单消抖
        if (P1 != 0xF7) {
            switch (P1 & 0xF0) {
                case 0x70: key = 1; break;
                case 0xB0: key = 2; break;
                case 0xD0: key = 3; break;
                case 0xE0: key = 4; break;
                default: break;
            }
        }
    }
    // 第二行扫描：P1.5输出低电平
    P1 = 0xFB;
    if (P1 != 0xFB) {
        DelayMsSoft(1);
        if (P1 != 0xFB) {
            switch (P1 & 0xF0) {
                case 0x70: key = 5; break;
                case 0xB0: key = 6; break;
                case 0xD0: key = 7; break;
                case 0xE0: key = 8; break;
                default: break;
            }
        }
    }
    // 第三行扫描：P1.6输出低电平
    P1 = 0xFD;
    if (P1 != 0xFD) {
        DelayMsSoft(1);
        if (P1 != 0xFD) {
            switch (P1 & 0xF0) {
                case 0x70: key = 9;  break;
                case 0xB0: key = 10; break;
                case 0xD0: key = 11; break;
                case 0xE0: key = 12; break;
                default: break;
            }
        }
    }
    // 第四行扫描：P1.7输出低电平
    P1 = 0xFE;
    if (P1 != 0xFE) {
        DelayMsSoft(1);
        if (P1 != 0xFE) {
            switch (P1 & 0xF0) {
                case 0x70: key = 13; break;
                case 0xB0: key = 14; break;
                case 0xD0: key = 15; break;
                case 0xE0: key = 16; break;
                default: break;
            }
        }
    }
    P1 = 0xFF; // 扫描完成，全部行置高电平
    return key;
}

// 带15ms硬件消抖处理，按下只返回一次按键值，长按不重复触发
static u8 Key_GetClick(void)
{
    static u8 last_raw = 0;       // 上一次读取到的原始按键值
    static u8 stable_key = 0;     // 消抖稳定后的按键值
    static u8 pressed_latch = 0;  // 按键按下锁存标志，防止连按
    static u32 last_change_ms = 0;// 按键电平变化记录时间戳
    u8 raw = Key_ReadRaw();
    u8 click = 0;

    // 检测按键电平发生变化，更新时间戳
    if (raw != last_raw) {
        last_raw = raw;
        last_change_ms = g_ms;
    }
    // 等待15ms消抖稳定
    if ((u16)(g_ms - last_change_ms) >= KEY_DEBOUNCE_MS) {
        if (stable_key != raw) {
            stable_key = raw;
            // 按键按下且无锁存，输出一次按键点击
            if (stable_key != 0 && pressed_latch == 0) {
                click = stable_key;
                pressed_latch = stable_key;
            }
            // 按键松开，清除锁存标志，允许下次点击
            if (stable_key == 0) {
                pressed_latch = 0;
            }
        }
    }
    return click;
}

// 关闭蜂鸣器，重置所有蜂鸣相关状态变量
static void Beep_Stop(void)
{
    TR1 = 0;                 // 关闭定时器1停止音调输出
    g_beep_enable = 0;
    BEEP = 1;                // 无源蜂鸣器高电平静音
    g_beep_mode = BEEP_IDLE;
    g_beep_elapsed = 0;
    g_beep_total = 0;
    g_beep_done = 1;
}

// 设置蜂鸣器发声频率，freq=0静音，非0启动定时器输出方波
static void Beep_SetFreq(u16 freq)
{
    if (freq == 0) {
        TR1 = 0;
        g_beep_enable = 0;
        BEEP = 1;
    } else {
        g_t1_reload = Timer1ReloadByFreq(freq);
        TH1 = g_t1_reload >> 8;
        TL1 = g_t1_reload & 0xFF;
        g_beep_enable = 1;
        TR1 = 1; // 启动定时器1产生方波驱动蜂鸣器
    }
}

// 启动单次固定时长提示音
static void Beep_StartOnce(u16 freq, u16 duration_ms)
{
    g_beep_mode = BEEP_ONCE;
    g_beep_elapsed = 0;
    g_beep_total = duration_ms;
    g_beep_done = 0;
    Beep_SetFreq(freq);
}

// 启动密码错误提示音：2000Hz断续蜂鸣，总时长300ms
static void Beep_StartError(void)
{
    g_beep_mode = BEEP_ERROR;
    g_beep_elapsed = 0;
    g_beep_total = 300;
    g_beep_done = 0;
    Beep_SetFreq(2000);
}

// 启动通关完整旋律播放
static void Beep_StartMelody(void)
{
    g_beep_mode = BEEP_MELODY;
    g_beep_elapsed = 0;
    g_melody_index = 0;
    g_beep_done = 0;
    Beep_SetFreq(MELODY_FREQ[0]);
}

// 初始化设置密码界面，重置所有输入相关变量，清空数码管
static void State_SetPassword_Init(void)
{
    u8 i;
    g_state = STATE_SET_PASSWORD;
    g_input_len = 0;
    g_verify_index = 0;
    g_roll_num = 1;
    g_roll_elapsed = 0;
    g_wait_beep_result = 0;
    g_pending_correct = 0;
    // 清空密码存储数组
    for (i = 0; i < PASSWORD_LEN; i++) {
        g_password[i] = 0;
    }
    Display_Clear();
}

// 初始化密码验证游戏界面，清空屏幕，第一位显示滚动数字1
static void State_Verify_Init(void)
{
    g_state = STATE_VERIFY_PASSWORD;
    g_verify_index = 0;
    g_roll_num = 1;
    g_roll_elapsed = 0;
    g_wait_beep_result = 0;
    g_pending_correct = 0;
    Display_Clear();
    Display_SetDigit(0, g_roll_num);
}

// 验证成功界面：数码管显示完整5位密码，播放通关旋律
static void State_Success_Init(void)
{
    u8 i;
    g_state = STATE_SUCCESS;
    Display_Clear();
    // 依次把5位密码显示在数码管前5位
    for (i = 0; i < PASSWORD_LEN; i++) {
        Display_SetDigit(i, g_password[i]);
    }
    Beep_StartMelody();
}

// 系统复位函数：停止蜂鸣器，切回设置密码初始界面
static void System_Reset(void)
{
    Beep_Stop();
    State_SetPassword_Init();
}

// 设置密码阶段按键处理函数
static void Handle_SetPassword(u8 key)
{
    if (key >= 1 && key <= 9) { // 数字1~9输入密码
        if (g_input_len < PASSWORD_LEN) { // 未输满5位允许录入
            g_password[g_input_len] = key;
            Display_SetDigit(g_input_len, key);
            g_input_len++;
            Beep_StartOnce(1000, 100); // 输入成功短鸣提示
        }
    } else if (key == 15) { // 15号键退格删除最后一位
        if (g_input_len > 0) {
            g_input_len--;
            g_password[g_input_len] = 0;
            Display_BlankDigit(g_input_len);
        }
    } else if (key == 16) { // 16号确认键，输满5位进入验证游戏
        if (g_input_len == PASSWORD_LEN) {
            State_Verify_Init();
        }
    }
}

// 密码验证阶段按键处理函数，仅16号确认键生效
static void Handle_Verify(u8 key)
{
    // 非确认键 或 当前正在播放蜂鸣，不处理按键
    if (key != 16 || g_wait_beep_result) {
        return;
    }
    // 当前滚动数字与密码对应位匹配
    if (g_roll_num == g_password[g_verify_index]) {
        g_pending_correct = 1;
        g_wait_beep_result = 1;
        Beep_StartOnce(500, 300);
    } else { // 数字不匹配，播放错误蜂鸣
        g_pending_correct = 0;
        g_wait_beep_result = 1;
        Beep_StartError();
    }
}

// 蜂鸣播放完成后执行后续逻辑：切换下一位密码验证
static void Verify_AfterBeep(void)
{
    // 未等待蜂鸣、蜂鸣未播放完成直接退出
    if (!g_wait_beep_result || !g_beep_done) {
        return;
    }
    g_wait_beep_result = 0;
    if (g_pending_correct) { // 当前位匹配成功
        Display_SetDigit(g_verify_index, g_roll_num); // 固定显示正确数字
        g_verify_index++;
        // 5位密码全部匹配，进入通关界面
        if (g_verify_index >= PASSWORD_LEN) {
            State_Success_Init();
        } else { // 切换到下一位，数字重置从1开始滚动
            g_roll_num = 1;
            g_roll_elapsed = 0;
            Display_SetDigit(g_verify_index, g_roll_num);
        }
    } else { // 当前位匹配失败，数字重置重新滚动
        g_roll_num = 1;
        g_roll_elapsed = 0;
        Display_SetDigit(g_verify_index, g_roll_num);
    }
}

// 1ms定时任务函数：数码管动态扫描、数字滚动计时、蜂鸣时长计时
static void Timer0_1ms_Task(void)
{
    P0 = SEG_BLANK; // 段选清零，防止切换位选时出现拖影
    Digit_Select(g_scan_pos); // 选中当前扫描数码管
    // 输出当前缓存段码到P0口
    if (g_disp[g_scan_pos] == SEG_BLANK) {
        P0 = SEG_BLANK;
    } else {
        P0 = g_disp[g_scan_pos];
    }
    g_scan_pos++;
    if (g_scan_pos >= DIGIT_COUNT) { // 8位扫描完成，回到第一位循环
        g_scan_pos = 0;
    }

    // 密码验证界面：自动数字滚动计时
    if (g_state == STATE_VERIFY_PASSWORD && !g_wait_beep_result) {
        g_roll_elapsed++;
        if (g_roll_elapsed >= 1000) { // 每1000ms数字+1
            g_roll_elapsed = 0;
            g_roll_num++;
            if (g_roll_num > 9) { // 超过9重置为1循环
                g_roll_num = 1;
            }
            Display_SetDigit(g_verify_index, g_roll_num);
        }
    }

    // 单次提示音计时控制
    if (g_beep_mode == BEEP_ONCE) {
        g_beep_elapsed++;
        if (g_beep_elapsed >= g_beep_total) {
            Beep_Stop();
        }
    }
    // 错误警示音分段控制：响100ms，静音100ms，总时长300ms停止
    else if (g_beep_mode == BEEP_ERROR) {
        g_beep_elapsed++;
        if (g_beep_elapsed == 100) {
            Beep_SetFreq(0);
        } else if (g_beep_elapsed == 200) {
            Beep_SetFreq(2000);
        } else if (g_beep_elapsed >= 300) {
            Beep_Stop();
        }
    }
    // 通关旋律播放：按音符时长切换下一个音调
    else if (g_beep_mode == BEEP_MELODY) {
        g_beep_elapsed++;
        if (g_beep_elapsed >= MELODY_DUR[g_melody_index]) {
            g_melody_index++;
            g_beep_elapsed = 0;
            if (MELODY_DUR[g_melody_index] == 0) { // 读到休止结束符，停止旋律
                Beep_Stop();
            } else {
                Beep_SetFreq(MELODY_FREQ[g_melody_index]);
            }
        }
    }
}

// 定时器0中断服务函数，1ms进一次中断
void Timer0_ISR(void) interrupt 1
{
    TH0 = T0_RELOAD_H; // 手动重装初值
    TL0 = T0_RELOAD_L;
    g_ms++;            // 系统毫秒计数器自增
    Timer0_1ms_Task(); // 执行1ms周期后台任务
}

// 定时器1中断服务函数，产生方波驱动无源蜂鸣器
void Timer1_ISR(void) interrupt 3
{
    TH1 = g_t1_reload >> 8; // 重装当前音调初值
    TL1 = g_t1_reload & 0xFF;
    if (g_beep_enable) {
        BEEP = !BEEP; // 电平翻转输出方波发声
    } else {
        BEEP = 1;     // 静音
    }
}

// 程序主入口函数
void main(void)
{
    u8 key;
    P0 = SEG_BLANK;  // 上电熄灭所有数码管段选
    P1 = 0xFF;       // 矩阵键盘行全部置高
    BEEP = 1;        // 上电蜂鸣器静音
    Timer_Init();    // 初始化定时器与中断
    State_SetPassword_Init(); // 进入设置密码初始界面
    // 主循环轮询扫描按键
    while (1) {
        key = Key_GetClick();
        if (key == 0) { // 无按键，检测蜂鸣完成后续逻辑
            Verify_AfterBeep();
            continue;
        }
        if (key == 14) { // 14号复位键，全局重置程序
            System_Reset();
            continue;
        }
        // 根据当前系统状态处理按键
        if (g_state == STATE_SET_PASSWORD) {
            Handle_SetPassword(key);
        } else if (g_state == STATE_VERIFY_PASSWORD) {
            Handle_Verify(key);
        }
        Verify_AfterBeep(); // 每次按键后校验蜂鸣完成逻辑
    }
}
