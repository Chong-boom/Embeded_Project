#include "Game_1.h"
#include "main.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"
#include "adc.h"
#include "LCD.h"
#include "PWM.h"
#include "Buzzer.h"
#include "../Joystick/Joystick.h"
#include "InputHandler.h"
#include "Menu.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define SCREEN_WIDTH       240
#define SCREEN_HEIGHT      240
#define CELL_SIZE          4
#define FOOD_SIZE          20
#define POISON_SIZE        16
#define MAX_SNAKE_LENGTH   256
#define FPS                8
#define FRAME_TIME_MS      (1000/FPS)
#define POISON_REFRESH_MS  5000
#define INVINCIBLE_DURATION_MS   30000
#define INVINCIBLE_BLINK_START_MS 15000

#define COLOR_BLACK    0
#define COLOR_GREEN    2
#define COLOR_RED      3
#define COLOR_YELLOW   4
#define COLOR_WHITE    1
#define COLOR_BLUE     5
#define COLOR_CYAN     6

typedef struct 
{ 
    int x; 
    int y; 
} Position;

typedef struct 
{
    Position body[MAX_SNAKE_LENGTH];
    int length;
    int dir_x;
    int dir_y;
} Snake;


static Snake skygame;
static Position food;
static Position food2;
static Position food3;
static Position gob;

static Joystick_t yaogan;
static uint8_t gameover = 0;
static int score = 0;

static uint32_t last_poison_time = 0;
static uint8_t  invinciblemode = 0;
static uint8_t  invincible_type = 0;
static uint32_t invincible_start_time = 0;

static uint8_t skyled = 0;
static uint8_t bps = 1;
static uint32_t blt = 0;

#define BTN_DEBOUNCE_MS 50

static uint8_t mode_select = 0;
static uint8_t chosenmode  = 0;
static Position bullet = {0, 0};
static uint8_t bulletmovement = 0;
static uint32_t invincible_duration_ms = 30000;


static void play_score_beep(int score_count)
{
    extern Buzzer_cfg_t buzzer_cfg;
    uint16_t frequency = 440 + (score_count * 10);
    
    if (frequency > 2000) 
        frequency = 2000;
        
    buzzer_tone(&buzzer_cfg, frequency, 50);
    HAL_Delay(80);
    buzzer_off(&buzzer_cfg);
}

static void play_gameover_beep(void)
{
    extern Buzzer_cfg_t buzzer_cfg;
    
    for (int i = 0; i < 3; i++) 
    {
        buzzer_tone(&buzzer_cfg, 800 - (i * 200), 50);
        HAL_Delay(100);
        buzzer_off(&buzzer_cfg);
        HAL_Delay(50);
    }
}

static void play_confirm_beep(void)
{
    extern Buzzer_cfg_t buzzer_cfg;
    
    buzzer_tone(&buzzer_cfg, 1000, 50);
    HAL_Delay(100);
    buzzer_off(&buzzer_cfg);
    HAL_Delay(50);
    
    buzzer_tone(&buzzer_cfg, 1200, 50);
    HAL_Delay(100);
    buzzer_off(&buzzer_cfg);
}

static void spawn_food(void) 
{
    food.x = (rand() % (SCREEN_WIDTH / FOOD_SIZE)) * FOOD_SIZE;
    food.y = (rand() % (SCREEN_HEIGHT / FOOD_SIZE)) * FOOD_SIZE;
}

static void spawn_food2(void) 
{
    food2.x = (rand() % (SCREEN_WIDTH / FOOD_SIZE)) * FOOD_SIZE;
    food2.y = (rand() % (SCREEN_HEIGHT / FOOD_SIZE)) * FOOD_SIZE;
}

static void spawn_food3(void) 
{
    food3.x = (rand() % (SCREEN_WIDTH / FOOD_SIZE)) * FOOD_SIZE;
    food3.y = (rand() % (SCREEN_HEIGHT / FOOD_SIZE)) * FOOD_SIZE;
}

static void spawn_poison(void) 
{
    gob.x = (rand() % (SCREEN_WIDTH / POISON_SIZE)) * POISON_SIZE;
    gob.y = (rand() % (SCREEN_HEIGHT / POISON_SIZE)) * POISON_SIZE;
    
    last_poison_time = HAL_GetTick();
}

static void init_game(void) 
{
    skygame.length = 3;
    
    skygame.body[0].x = 20;
    skygame.body[0].y = 20;
    skygame.body[1].x = skygame.body[0].x - CELL_SIZE;
    skygame.body[1].y = skygame.body[0].y;
    skygame.body[2].x = skygame.body[1].x - CELL_SIZE;
    skygame.body[2].y = skygame.body[1].y;
    
    skygame.dir_x = CELL_SIZE;
    skygame.dir_y = 0;
    
    score          = 0;
    invinciblemode = 0;
    invincible_type= 0;
    skyled         = 0;
    mode_select    = 0;
    chosenmode     = 0;
    bulletmovement = 0;
    gameover       = 0;
    
    extern PWM_cfg_t pwm_cfg;
    PWM_Off(&pwm_cfg);
    
    spawn_food();
    spawn_poison();
}

static void update_game(void) 
{
    extern MenuSystem menu;
    extern InputState current_input;
    extern PWM_cfg_t pwm_cfg;
    extern Buzzer_cfg_t buzzer_cfg;
    extern Joystick_cfg_t joystick_cfg;
    
    if (current_input.btn3_pressed)
    {
        Menu_BackToHome(&menu);
        return;
    }

    if (gameover)
    {
        Menu_BackToHome(&menu);
        return;
    }

    uint32_t now = HAL_GetTick();
    uint8_t btn_curr_state = HAL_GPIO_ReadPin(BTN2_GPIO_Port, BTN2_Pin);
    static uint8_t btn_triggered = 0;


    if(btn_curr_state != bps)
    {
        blt = now;
        bps = btn_curr_state;
        btn_triggered = 0;
    }
    else 
    {
        if(now - blt >= BTN_DEBOUNCE_MS && !btn_triggered)
        {
            if(!btn_curr_state)
            {
                if(mode_select)
                {
                    invinciblemode = 1;
                    invincible_type = chosenmode;
                    invincible_start_time = now;
                    mode_select = 0;
                    
                    play_confirm_beep();
                    
                    if(invincible_type == 1)
                    {
                        spawn_food2();
                        spawn_food3();
                    }
                    else if(invincible_type == 3)
                    {
                        bullet.x = skygame.body[0].x;
                        bullet.y = skygame.body[0].y;
                        bulletmovement = 0;
                    }
                }
                else if(!invinciblemode)
                {
                    mode_select = 1;
                    chosenmode  = 1;
                    
                    buzzer_tone(&buzzer_cfg, 800, 50);
                    HAL_Delay(50);
                    buzzer_off(&buzzer_cfg);
                }
                else if(invinciblemode && invincible_type == 3)
                {
                    bullet.x = skygame.body[0].x;
                    bullet.y = skygame.body[0].y;
                    bulletmovement = 1;
                    
                    buzzer_tone(&buzzer_cfg, 1200, 50);
                    HAL_Delay(50);
                    buzzer_off(&buzzer_cfg);
                }
                btn_triggered = 1;
            }
        }
    }


    if(mode_select)
    {
        static uint32_t last_mode_change = 0;
        uint32_t mode_change_delay = 500;
        
        if(now - last_mode_change >= mode_change_delay)
        {
            Joystick_Read(&joystick_cfg, &yaogan);
            UserInput input = Joystick_GetInput(&yaogan);
            
            if(input.direction == N) 
            {
                chosenmode = 1;
                last_mode_change = now;
                buzzer_tone(&buzzer_cfg, 600, 50);
                HAL_Delay(30);
                buzzer_off(&buzzer_cfg);
            }
            else if(input.direction == S) 
            {
                chosenmode = 3;
                last_mode_change = now;
                buzzer_tone(&buzzer_cfg, 600, 50);
                HAL_Delay(30);
                buzzer_off(&buzzer_cfg);
            }
            else if(input.direction == E && chosenmode < 3) 
            {
                chosenmode++;
                last_mode_change = now;
                buzzer_tone(&buzzer_cfg, 600, 50);
                HAL_Delay(30);
                buzzer_off(&buzzer_cfg);
            }
            else if(input.direction == W && chosenmode > 1) 
            {
                chosenmode--;
                last_mode_change = now;
                buzzer_tone(&buzzer_cfg, 600, 50);
                HAL_Delay(30);
                buzzer_off(&buzzer_cfg);
            }
        }
        return;
    }


    if(invinciblemode)
    {
        uint32_t invincible_elapsed = now - invincible_start_time;
        
        if(invincible_elapsed >= invincible_duration_ms)
        {
            invinciblemode  = 0;
            invincible_type = 0;
            PWM_Off(&pwm_cfg);
        }
        else if(invincible_elapsed >= (invincible_duration_ms - INVINCIBLE_BLINK_START_MS))
        {
            uint32_t remaining = invincible_duration_ms - invincible_elapsed;
            uint32_t blink_freq = 2 + (remaining * (20 - 2)) / INVINCIBLE_BLINK_START_MS;
            static uint32_t last_blink_change = 0;
            uint32_t blink_period = 1000 / blink_freq;
            
            if(now - last_blink_change >= blink_period / 2)
            {
                if(skyled)
                {
                    PWM_Off(&pwm_cfg);
                    skyled = 0;
                }
                else 
                {
                    PWM_Set(&pwm_cfg, blink_freq, 50);
                    skyled = 1;
                }
                last_blink_change = now;
            }
        }
        else 
        {
            PWM_Set(&pwm_cfg, 1000, 100);
            skyled = 1;
        }
    }


    Joystick_Read(&joystick_cfg, &yaogan);
    UserInput input = Joystick_GetInput(&yaogan);
    
    switch(input.direction)
    {
        case N: 
            if(skygame.dir_y == 0) 
            { 
                skygame.dir_x = 0; 
                skygame.dir_y = -CELL_SIZE; 
            } 
            break;
            
        case S: 
            if(skygame.dir_y == 0) 
            { 
                skygame.dir_x = 0; 
                skygame.dir_y = CELL_SIZE; 
            } 
            break;
            
        case W: 
            if(skygame.dir_x == 0) 
            { 
                skygame.dir_x = -CELL_SIZE; 
                skygame.dir_y = 0; 
            } 
            break;
            
        case E: 
            if(skygame.dir_x == 0) 
            { 
                skygame.dir_x = CELL_SIZE; 
                skygame.dir_y = 0; 
            } 
            break;
            
        default: break;
    }


    for(int i = skygame.length - 1; i > 0; i--)
    {
        skygame.body[i] = skygame.body[i-1];
    }
    skygame.body[0].x += skygame.dir_x;
    skygame.body[0].y += skygame.dir_y;


    if(skygame.body[0].x < 0 || skygame.body[0].x >= SCREEN_WIDTH ||
       skygame.body[0].y < 0 || skygame.body[0].y >= SCREEN_HEIGHT)
    {
        gameover = 1;
        play_gameover_beep();
        return;
    }


    for(int i = 1; i < skygame.length; i++) 
    {
        if(skygame.body[0].x == skygame.body[i].x && skygame.body[0].y == skygame.body[i].y)
        {
            gameover = 1;
            play_gameover_beep();
            return;
        }
    }


    if(!invinciblemode)
    {
        if(skygame.body[0].x >= gob.x && skygame.body[0].x < gob.x + POISON_SIZE &&
           skygame.body[0].y >= gob.y && skygame.body[0].y < gob.y + POISON_SIZE)
        {
            gameover = 1;
            play_gameover_beep();
            return;
        }
    }


    if(skygame.body[0].x >= food.x && skygame.body[0].x < food.x + FOOD_SIZE &&
       skygame.body[0].y >= food.y && skygame.body[0].y < food.y + FOOD_SIZE)
    {
        if(skygame.length < MAX_SNAKE_LENGTH - 1)
            skygame.length += 2;
        else if(skygame.length < MAX_SNAKE_LENGTH)
            skygame.length++;
            
        score++;
        play_score_beep(score);
        spawn_food();
    }
    

    if(invinciblemode && invincible_type == 1)
    {
        if(skygame.body[0].x >= food2.x && skygame.body[0].x < food2.x + FOOD_SIZE &&
           skygame.body[0].y >= food2.y && skygame.body[0].y < food2.y + FOOD_SIZE)
        {
            if(skygame.length < MAX_SNAKE_LENGTH - 1)
                skygame.length += 2;
            else if(skygame.length < MAX_SNAKE_LENGTH)
                skygame.length++;
                
            score++;
            play_score_beep(score);
            spawn_food2();
        }
        
        if(skygame.body[0].x >= food3.x && skygame.body[0].x < food3.x + FOOD_SIZE &&
           skygame.body[0].y >= food3.y && skygame.body[0].y < food3.y + FOOD_SIZE)
        {
            if(skygame.length < MAX_SNAKE_LENGTH - 1)
                skygame.length += 2;
            else if(skygame.length < MAX_SNAKE_LENGTH)
                skygame.length++;
                
            score++;
            play_score_beep(score);
            spawn_food3();
        }
    }


    if(invinciblemode && invincible_type == 2)
    {
        int circle_radius = 30;
        int dx = food.x + FOOD_SIZE/2 - (skygame.body[0].x + CELL_SIZE/2);
        int dy = food.y + FOOD_SIZE/2 - (skygame.body[0].y + CELL_SIZE/2);
        
        if(dx*dx + dy*dy <= circle_radius*circle_radius)
        {
            if(skygame.length < MAX_SNAKE_LENGTH - 1)
                skygame.length += 2;
            else if(skygame.length < MAX_SNAKE_LENGTH)
                skygame.length++;
                
            score++;
            play_score_beep(score);
            spawn_food();
        }
    }


    if(invinciblemode && invincible_type == 3 && bulletmovement)
    {
        bullet.x += skygame.dir_x * 2;
        bullet.y += skygame.dir_y * 2;
        
        if(bullet.x < 0 || bullet.x >= SCREEN_WIDTH || 
           bullet.y < 0 || bullet.y >= SCREEN_HEIGHT)
        {
            bulletmovement = 0;
        }
        
        if(bulletmovement)
        {
            if(bullet.x >= food.x && bullet.x < food.x + FOOD_SIZE &&
               bullet.y >= food.y && bullet.y < food.y + FOOD_SIZE)
            {
                score++;
                play_score_beep(score);
                spawn_food();
                bulletmovement = 0;
            }
        }
    }


    if((now - last_poison_time) >= POISON_REFRESH_MS)
    {
        spawn_poison();
    }
}

static void render_game_over(void) 
{
    extern ST7789V2_cfg_t cfg0;
    
    LCD_Fill_Buffer(COLOR_BLACK);
    char buf[32];
    
    LCD_printString("GAME OVER", 50, 60, 1, 3);
    
    sprintf(buf, "Score: %d", score);
    LCD_printString(buf, 70, 120, 1, 2);
    
    LCD_printString("Press BT3", 60, 180, 1, 2);
    LCD_printString("to return", 55, 200, 1, 2);
    
    LCD_Refresh(&cfg0);
}

static void render_game(void) 
{
    extern ST7789V2_cfg_t cfg0;
    
    LCD_Fill_Buffer(COLOR_BLACK);
    char buf[32];
    
    if(mode_select)
    {
        LCD_printString("SELECT POWER-UP", 50, 5, 1, 2);
        
        int part_width = SCREEN_WIDTH / 3;
        
        for(int i = 0; i < 3; i++)
        {
            uint16_t bg_color = (chosenmode == i+1) ? COLOR_YELLOW : COLOR_BLACK;
            uint16_t border_color = (chosenmode == i+1) ? COLOR_RED : COLOR_WHITE;
            
            LCD_Draw_Rect(i*part_width, 35, part_width-3, 100, bg_color, 0);
            LCD_Draw_Rect(i*part_width, 35, part_width-3, 2, border_color, 0);
            LCD_Draw_Rect(i*part_width, 133, part_width-3, 2, border_color, 0);
            LCD_Draw_Rect(i*part_width, 35, 2, 100, border_color, 0);
            LCD_Draw_Rect((i+1)*part_width-2, 35, 2, 100, border_color, 0);
            
            if(i == 0)
            {
                LCD_printString("[1]", i*part_width + 25, 55, 1, 2);
                LCD_printString("3 FOOD", i*part_width + 10, 75, 1, 2);
                LCD_printString("MORE", i*part_width + 15, 95, 1, 2);
            }
            else if(i == 1)
            {
                LCD_printString("[2]", i*part_width + 25, 55, 1, 2);
                LCD_printString("CIRCLE", i*part_width + 8, 75, 1, 2);
                LCD_printString("RANGE", i*part_width + 10, 95, 1, 2);
            }
            else 
            {
                LCD_printString("[3]", i*part_width + 25, 55, 1, 2);
                LCD_printString("BULLET", i*part_width + 8, 75, 1, 2);
                LCD_printString("SHOOT", i*part_width + 12, 95, 1, 2);
            }
        }
        
        LCD_printString("JOYSTICK: UP/DOWN", 30, 150, 1, 1);
        LCD_printString("LEFT/RIGHT", 80, 165, 1, 1);
        LCD_printString("BTN: CONFIRM", 70, 185, 1, 2);
        
        if(chosenmode == 1)
        {
            LCD_Draw_Rect(25, 145, 8, 8, COLOR_RED, 1);
            LCD_Draw_Rect(55, 145, 8, 8, COLOR_RED, 1);
        }
        else if(chosenmode == 2)
        {
            LCD_Draw_Rect(105, 145, 8, 8, COLOR_RED, 1);
            LCD_Draw_Rect(135, 145, 8, 8, COLOR_RED, 1);
        }
        else 
        {
            LCD_Draw_Rect(185, 145, 8, 8, COLOR_RED, 1);
            LCD_Draw_Rect(215, 145, 8, 8, COLOR_RED, 1);
        }
        
        LCD_Refresh(&cfg0);
        return;
    }
    
    uint8_t snake_color = invinciblemode ? COLOR_YELLOW : COLOR_GREEN;
    
    for(int i = 0; i < skygame.length; i++) 
    {
        LCD_Draw_Rect(skygame.body[i].x, skygame.body[i].y, CELL_SIZE, CELL_SIZE, snake_color, 1);
    }
    
    if(invinciblemode && invincible_type == 2)
    {
        int center_x = skygame.body[0].x + CELL_SIZE/2;
        int center_y = skygame.body[0].y + CELL_SIZE/2;
        int radius = 30;
        
        for(int angle = 0; angle < 360; angle += 10)
        {
            int x = center_x + radius * cos(angle * 3.14159 / 180);
            int y = center_y + radius * sin(angle * 3.14159 / 180);
            LCD_Draw_Rect(x-1, y-1, 3, 3, COLOR_BLUE, 1);
        }
    }
    
    if(invinciblemode && invincible_type == 3 && bulletmovement)
    {
        LCD_Draw_Rect(bullet.x, bullet.y, 8, 8, COLOR_CYAN, 1);
    }
    
    LCD_Draw_Rect(food.x, food.y, FOOD_SIZE, FOOD_SIZE, COLOR_YELLOW, 1);
    
    if(invinciblemode && invincible_type == 1)
    {
        LCD_Draw_Rect(food2.x, food2.y, FOOD_SIZE, FOOD_SIZE, COLOR_YELLOW, 1);
        LCD_Draw_Rect(food3.x, food3.y, FOOD_SIZE, FOOD_SIZE, COLOR_YELLOW, 1);
    }
    
    LCD_Draw_Rect(gob.x, gob.y, POISON_SIZE, POISON_SIZE, COLOR_RED, 1);
    
    sprintf(buf,"Score:%d",score);
    LCD_printString(buf, 5, 5, 1, 2);
    
    if(invinciblemode)
    {
        uint32_t remaining = (invincible_start_time + invincible_duration_ms - HAL_GetTick()) / 1000;
        sprintf(buf,"Mode%d:%ds", invincible_type, (int)remaining);
        LCD_printString(buf, 5, 20, 1, 2);
    }
    
    LCD_Refresh(&cfg0);
}

MenuState Game1_Run(void) 
{
    extern ST7789V2_cfg_t cfg0;
    
    srand(HAL_GetTick());
    init_game();
    
    MenuState exit_state = MENU_STATE_HOME;
    
    LCD_Fill_Buffer(COLOR_BLACK);
    LCD_Refresh(&cfg0);

    while (1) 
    {
        uint32_t frame_start = HAL_GetTick();
        
        Input_Read();
        
        if(gameover)
        {
            render_game_over();
            if(current_input.btn3_pressed)
            {
                exit_state = MENU_STATE_HOME;
                break;
            }
            HAL_Delay(FRAME_TIME_MS);
            continue;
        }
        
        update_game();
        
        if(g_menu_back_to_home) 
        {
            exit_state = MENU_STATE_HOME;
            break;
        }
        
        render_game();
        
        uint32_t frame_time = HAL_GetTick() - frame_start;
        if (frame_time < FRAME_TIME_MS) 
        {
            HAL_Delay(FRAME_TIME_MS - frame_time);
        }
    }
    
    return exit_state;
}