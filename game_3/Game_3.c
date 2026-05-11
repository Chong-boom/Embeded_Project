#include "Game_3.h"
#include "InputHandler.h"
#include "Menu.h"
#include "LCD.h"
#include "PWM.h"
#include "Buzzer.h"
#include "Joystick.h"
#include "Utils.h"
#include "stm32l4xx_hal.h"
#include <stdio.h>
#include <stdint.h>
#include <math.h>

extern ST7789V2_cfg_t cfg0;
extern PWM_cfg_t pwm_cfg;
extern Buzzer_cfg_t buzzer_cfg;
extern Joystick_cfg_t joystick_cfg;
extern Joystick_t joystick_data;

// ====================== Level 1: Forest Defense ======================
#define GAME3_FRAME_TIME_MS  16
#define MAX_BULLETS          12
#define MAX_MONSTERS         4
#define BULLET_SPEED         4
#define MONSTER_SPEED        1
#define SHOOT_COOLDOWN_MS    500
#define DAMAGE_COOLDOWN_MS   1000
#define MAX_HP               3
#define KILL_TO_WIN          10

// Player
static int16_t player_x = 120;
static int16_t player_y = 120;
static uint8_t player_hp = MAX_HP;
static uint8_t kill_count = 0;

typedef enum {
    DIR_NONE = 0,
    DIR_N, DIR_S, DIR_W, DIR_E,
    DIR_NW, DIR_NE, DIR_SW, DIR_SE
} BulletDir;

static BulletDir last_move_dir = DIR_NONE;

typedef struct {
    int16_t x, y;
    int8_t dx, dy;
    uint8_t alive;
} Bullet;

typedef struct {
    int16_t x, y;
    uint8_t alive;
} Monster;

static Bullet bullets[MAX_BULLETS];
static Monster monsters[MAX_MONSTERS];
static uint32_t last_spawn_time = 0;
static uint32_t last_shoot_time = 0;
static uint32_t last_damage_time1 = 0;   // Level 1 only
static uint32_t frame_counter = 0;

// ====================== Level 1 Victory Effects ======================
static void Victory_Fireworks(void) {
    for (int i = 0; i < MAX_BULLETS; i++) bullets[i].alive = 0;

    int8_t dirs[8][2] = {{0,-6},{0,6},{-6,0},{6,0},{-4,-4},{4,-4},{-4,4},{4,4}};

    for (int wave = 0; wave < 2; wave++) {
        for (int d = 0; d < 8; d++) {
            for (int i = 0; i < MAX_BULLETS; i++) {
                if (!bullets[i].alive) {
                    bullets[i].x = player_x;
                    bullets[i].y = player_y;
                    bullets[i].dx = dirs[d][0];
                    bullets[i].dy = dirs[d][1];
                    bullets[i].alive = 1;
                    break;
                }
            }
        }

        buzzer_note(&buzzer_cfg, NOTE_C5, 60); HAL_Delay(70);
        buzzer_note(&buzzer_cfg, NOTE_E5, 60); HAL_Delay(70);
        buzzer_note(&buzzer_cfg, NOTE_G5, 80);

        for (int f = 0; f < 12; f++) {
            for (int i = 0; i < MAX_BULLETS; i++) {
                if (!bullets[i].alive) continue;
                bullets[i].x += bullets[i].dx;
                bullets[i].y += bullets[i].dy;

                for (int j = 0; j < MAX_MONSTERS; j++) {
                    if (!monsters[j].alive) continue;
                    int16_t dx = bullets[i].x - monsters[j].x;
                    int16_t dy = bullets[i].y - monsters[j].y;
                    if (dx < 0) dx = -dx;
                    if (dy < 0) dy = -dy;
                    if (dx < 12 && dy < 12) {
                        bullets[i].alive = 0;
                        monsters[j].alive = 0;
                    }
                }
            }

            LCD_Fill_Buffer(0x0000);
            LCD_printString("VICTORY!", 65, 80, 1, 3);
            LCD_printString("P", player_x, player_y, 1, 3);
            for (int i = 0; i < MAX_BULLETS; i++) {
                if (bullets[i].alive) LCD_printString(".", bullets[i].x, bullets[i].y, 1, 2);
            }
            for (int i = 0; i < MAX_MONSTERS; i++) {
                if (monsters[i].alive) LCD_printString("M", monsters[i].x, monsters[i].y, 1, 3);
            }
            LCD_Refresh(&cfg0);
            HAL_Delay(40);
        }
    }
    buzzer_off(&buzzer_cfg);
}

// ====================== Level 2: Mine Cave Flight Challenge ======================
#define GAME_FRAME_TIME_MS   16
#define SURVIVAL_TIME_SEC    25
#define MAX_SPEED_LEVEL      8
#define MAX_PIPES            5
#define DAMAGE_COOLDOWN_MS2  800     // Level 2 collision cooldown

typedef struct {
    int16_t x;
    int16_t gap_y;
    uint8_t alive;
} Pipe;

static Pipe    pipes[MAX_PIPES];
static int16_t bird_x  = 60;
static int16_t bird_y  = 120;
static int8_t  bird_dy = 0;
static uint32_t last_pipe_time   = 0;
static uint32_t game_start_time  = 0;
static uint8_t  speed_level      = 1;
static uint32_t frame_counter2   = 0;
static uint8_t  player_hp2       = MAX_HP;
static uint32_t last_damage_time2 = 0;

// Screen/Pipe Constants
#define SCREEN_TOP      20
#define SCREEN_BOTTOM   220
#define PIPE_WIDTH      16
#define GAP_HALF        55

// ====================== Level 2 Failure Handling ======================
static void Game_Fail(void) {
    LCD_Fill_Buffer(0x0000);
    LCD_printString("GAME OVER", 20, 30, 1, 3);
    char buf[32];
    sprintf(buf, "Survived: %lus", (HAL_GetTick() - game_start_time) / 1000);
    LCD_printString(buf, 15, 60, 1, 2);
    LCD_printString("BT3 to return", 10, 90, 1, 2);
    LCD_Refresh(&cfg0);

    buzzer_tone(&buzzer_cfg, 800, 100); HAL_Delay(150);
    buzzer_tone(&buzzer_cfg, 400, 150);
    buzzer_off(&buzzer_cfg);

    while (1) {
        Input_Read();
        if (current_input.btn3_pressed) break;
    }
}

// ====================== Level 1 Intro Sequence ======================
static void Show_Intro_Level1(void) {
    LCD_Fill_Buffer(0x0000);
    LCD_printString("THE VILLAGE", 10, 20, 1, 3);
    LCD_printString("IS ATTACKED", 10, 45, 1, 3);
    LCD_printString("Enemies incoming...", 10, 70, 1, 2);
    LCD_printString("Protect the forest!", 10, 90, 1, 2);
    LCD_printString("Press BT3 to start", 15, 120, 1, 2);
    LCD_Refresh(&cfg0);

    buzzer_note(&buzzer_cfg, NOTE_G4, 150); HAL_Delay(180);
    buzzer_note(&buzzer_cfg, NOTE_A4, 150); HAL_Delay(180);
    buzzer_note(&buzzer_cfg, NOTE_B4, 200); HAL_Delay(250);
    buzzer_note(&buzzer_cfg, NOTE_G4, 150); HAL_Delay(180);
    buzzer_note(&buzzer_cfg, NOTE_A4, 150); HAL_Delay(180);
    buzzer_note(&buzzer_cfg, NOTE_B4, 250); HAL_Delay(400);
    buzzer_off(&buzzer_cfg);

    while (1) {
        Input_Read();
        if (current_input.btn3_pressed) break;
        HAL_Delay(10);
    }

    buzzer_tone(&buzzer_cfg, 1200, 80);
    HAL_Delay(100);
    buzzer_off(&buzzer_cfg);
    HAL_Delay(300);
}

// ====================== Level 2 Intro Sequence ======================
static void Show_Intro_Level2(void) {
    LCD_Fill_Buffer(0x0000);
    LCD_printString("LEVEL 2", 30, 20, 1, 3);
    LCD_printString("MINE CAVE", 25, 45, 1, 3);
    LCD_printString("Navigate the mine", 10, 70, 1, 2);
    LCD_printString("Survive 25 seconds!", 10, 90, 1, 2);
    LCD_printString("Press BT3 to start", 15, 120, 1, 2);
    LCD_Refresh(&cfg0);

    buzzer_tone(&buzzer_cfg, 800, 120);  HAL_Delay(150);
    buzzer_tone(&buzzer_cfg, 600, 180);  HAL_Delay(200);
    buzzer_off(&buzzer_cfg);

    while (1) {
        Input_Read();
        if (current_input.btn3_pressed) break;
        HAL_Delay(10);
    }

    buzzer_tone(&buzzer_cfg, 1200, 80);
    HAL_Delay(100);
    buzzer_off(&buzzer_cfg);
    HAL_Delay(200);
}

// ====================== Level 3: Boss Battle ======================
#define L3_FRAME_MS           16

// Player
#define L3_PLAYER_HP          5          
#define L3_PLAYER_SPEED       3

// Normal Bullets
#define L3_MAX_P_BULLETS      8
#define L3_P_BULLET_SPEED     5
#define L3_SHOOT_COOLDOWN_MS  300

// Boss
#define L3_BOSS_HP            20
#define L3_BOSS_SPEED         1
#define L3_BOSS_SIZE          35
#define L3_MAX_B_BULLETS      6
#define L3_B_BULLET_SPEED     3
#define L3_BOSS_SHOOT_CD_MS   800

// Damage Cooldown
#define L3_PLAYER_DMG_CD_MS   800

// Screen Boundaries
#define L3_SCR_LEFT    10
#define L3_SCR_RIGHT   230
#define L3_SCR_TOP     30
#define L3_SCR_BOTTOM  220

// -------- Data Structures --------
typedef struct {
    int16_t x, y;
    int8_t  dx, dy;
    uint8_t alive;
} L3_Bullet;

// -------- Static Variables --------
static int16_t  l3_px, l3_py;
static uint8_t  l3_php;

static int16_t  l3_bx, l3_by;
static int16_t  l3_bhp;
static int8_t   l3_bdx, l3_bdy;

static L3_Bullet l3_pbullets[L3_MAX_P_BULLETS];
static L3_Bullet l3_bbullets[L3_MAX_B_BULLETS];

static uint32_t l3_last_shoot_ms;
static uint32_t l3_boss_shoot_ms;
static uint32_t l3_last_dmg_ms;
static uint32_t l3_frame;
static uint8_t l3_boss_shoot_count;

// -------- Boss Shoots at Player --------
static void l3_boss_shoot(void) {
    l3_boss_shoot_count++;
    
    int bullets_fired = 0;
    int max_bullets = 1; // Default: fire 1 bullet
    
    // Fire spread pattern of 3 bullets every 3 shots
    if (l3_boss_shoot_count % 3 == 0) {
        max_bullets = 3;
        l3_boss_shoot_count = 0; // Reset counter
    }
    
    for (int i = 0; i < L3_MAX_B_BULLETS && bullets_fired < max_bullets; i++) {
        if (l3_bbullets[i].alive) continue;

        l3_bbullets[i].x = l3_bx;
        l3_bbullets[i].y = l3_by;
        l3_bbullets[i].alive = 1;

        int16_t vx = l3_px - l3_bx;
        int16_t vy = l3_py - l3_by;

        float angle;
        if (max_bullets == 3) {
            // Fire spread bullets with angle offset
            int16_t offset = (bullets_fired - 1) * 20; // -20, 0, +20 degrees
            angle = atan2(vy, vx) + offset * 3.14159 / 180;
        } else {
            // Fire single bullet directly at player
            angle = atan2(vy, vx);
        }
        
        l3_bbullets[i].dx = (int8_t)(cos(angle) * L3_B_BULLET_SPEED);
        l3_bbullets[i].dy = (int8_t)(sin(angle) * L3_B_BULLET_SPEED);
        
        bullets_fired++;
    }
}

// -------- Level 3 Intro Sequence --------
static void L3_Show_Intro(void) {
    LCD_Fill_Buffer(0x0000);
    LCD_printString("LEVEL 3",          30,  20, 1, 3);
    LCD_printString("BOSS BATTLE",      15,  45, 1, 3);
    LCD_printString("Defeat the BOSS!",  10,  70, 1, 2);
    LCD_printString("BT2: Shoot",       15,  90, 1, 2);
    LCD_printString("Press BT3 to start",15,  120, 1, 2);
    LCD_Refresh(&cfg0);

    buzzer_note(&buzzer_cfg, NOTE_E4, 120); HAL_Delay(150);
    buzzer_note(&buzzer_cfg, NOTE_G4, 120); HAL_Delay(150);
    buzzer_note(&buzzer_cfg, NOTE_B4, 200); HAL_Delay(250);
    buzzer_note(&buzzer_cfg, NOTE_E5, 250); HAL_Delay(300);
    buzzer_off(&buzzer_cfg);

    while (1) {
        Input_Read();
        if (current_input.btn3_pressed) break;
        HAL_Delay(10);
    }
    buzzer_tone(&buzzer_cfg, 1200, 80);
    HAL_Delay(100);
    buzzer_off(&buzzer_cfg);
    HAL_Delay(200);
}

// -------- Level 3 Victory Effects --------
static void L3_Victory(void) {
    for (int f = 0; f < 20; f++) {
        LCD_Fill_Buffer(0x0000);
        LCD_printString("BOSS DEFEATED!", 15,  10, 1, 2);
        LCD_printString("YOU WIN!!",       30,  25, 1, 2);
        LCD_printString("BT3 to return",   20,  40, 1, 1);

        for (int s = 0; s < 6; s++) {
            int16_t sx = (int16_t)Random_U16(200) + 10;
            int16_t sy = (int16_t)Random_U16(180) + 20;
            LCD_printString("*", sx, sy, 1, 2);
        }
        LCD_Refresh(&cfg0);

        buzzer_note(&buzzer_cfg, NOTE_C5, 50); HAL_Delay(60);
        buzzer_note(&buzzer_cfg, NOTE_E5, 50); HAL_Delay(60);
        buzzer_note(&buzzer_cfg, NOTE_G5, 60); HAL_Delay(80);
        buzzer_off(&buzzer_cfg);
        HAL_Delay(40);
    }

    while (1) {
        Input_Read();
        if (current_input.btn3_pressed) break;
        HAL_Delay(10);
    }
}

// -------- Level 3 Game Over Screen --------
static void L3_GameOver(void) {
    LCD_Fill_Buffer(0x0000);
    LCD_printString("GAME OVER",    20,  30, 1, 3);
    LCD_printString("BOSS survived",15,  60, 1, 2);

    char buf[32];
    sprintf(buf, "Boss HP left: %d", l3_bhp);
    LCD_printString(buf, 15,  85, 1, 2);
    LCD_printString("BT3 to return", 10,  110, 1, 2);
    LCD_Refresh(&cfg0);

    buzzer_tone(&buzzer_cfg, 800, 100); HAL_Delay(150);
    buzzer_tone(&buzzer_cfg, 400, 150); HAL_Delay(200);
    buzzer_off(&buzzer_cfg);

    while (1) {
        Input_Read();
        if (current_input.btn3_pressed) break;
        HAL_Delay(10);
    }
}

// ====================== Main Game Entry (Level 1 -> Level 2 -> Level 3) ======================
MenuState Game3_Run(void) {
    MenuState exit_state = MENU_STATE_HOME;

    // ==================== Level 1 ====================
    Show_Intro_Level1();

    // Level 1 initialization
    player_x = 120; player_y = 120;
    player_hp = MAX_HP;
    kill_count = 0;
    last_move_dir = DIR_NONE;
    frame_counter = 0;
    last_shoot_time = 0;
    last_damage_time1 = 0;
    for (int i = 0; i < MAX_BULLETS; i++) bullets[i].alive = 0;
    for (int i = 0; i < MAX_MONSTERS; i++) monsters[i].alive = 0;
    last_spawn_time = HAL_GetTick();

    while (1) {
        uint32_t frame_start = HAL_GetTick();
        frame_counter++;

        Input_Read();
        Joystick_Read(&joystick_cfg, &joystick_data);

        if (current_input.btn3_pressed) {
            exit_state = MENU_STATE_HOME;
            break;
        }

        // Player movement
        switch (joystick_data.direction) {
            case N:  player_y -= 2; last_move_dir = DIR_N;  break;
            case S:  player_y += 2; last_move_dir = DIR_S;  break;
            case W:  player_x -= 2; last_move_dir = DIR_W;  break;
            case E:  player_x += 2; last_move_dir = DIR_E;  break;
            case NW: player_x -= 2; player_y -= 2; last_move_dir = DIR_NW; break;
            case NE: player_x += 2; player_y -= 2; last_move_dir = DIR_NE; break;
            case SW: player_x -= 2; player_y += 2; last_move_dir = DIR_SW; break;
            case SE: player_x += 2; player_y += 2; last_move_dir = DIR_SE; break;
            default: break;
        }

        // Boundary constraints
        if (player_x < 10)  player_x = 10;
        if (player_x > 220) player_x = 220;
        if (player_y < 20)  player_y = 20;
        if (player_y > 220) player_y = 220;

        // BTN2 Shoot
        if (current_input.btn2_pressed) {
            uint32_t now = HAL_GetTick();
            if (now - last_shoot_time > SHOOT_COOLDOWN_MS) {
                for (int i = 0; i < MAX_BULLETS; i++) {
                    if (!bullets[i].alive) {
                        bullets[i].x = player_x;
                        bullets[i].y = player_y;
                        bullets[i].alive = 1;
                        switch (last_move_dir) {
                            case DIR_N:  bullets[i].dx =  0; bullets[i].dy =  BULLET_SPEED; break;
                            case DIR_S:  bullets[i].dx =  0; bullets[i].dy = -BULLET_SPEED; break;
                            case DIR_W:  bullets[i].dx =  BULLET_SPEED; bullets[i].dy = 0;  break;
                            case DIR_E:  bullets[i].dx = -BULLET_SPEED; bullets[i].dy = 0;  break;
                            case DIR_NW: bullets[i].dx =  BULLET_SPEED; bullets[i].dy =  BULLET_SPEED; break;
                            case DIR_NE: bullets[i].dx = -BULLET_SPEED; bullets[i].dy =  BULLET_SPEED; break;
                            case DIR_SW: bullets[i].dx =  BULLET_SPEED; bullets[i].dy = -BULLET_SPEED; break;
                            case DIR_SE: bullets[i].dx = -BULLET_SPEED; bullets[i].dy = -BULLET_SPEED; break;
                            default:     bullets[i].dx =  0; bullets[i].dy =  BULLET_SPEED; break;
                        }
                        last_shoot_time = now;
                        buzzer_tone(&buzzer_cfg, 1800, 20);
                        HAL_Delay(10);
                        buzzer_off(&buzzer_cfg);
                        break;
                    }
                }
            }
        }

        // Update bullets
        for (int i = 0; i < MAX_BULLETS; i++) {
            if (!bullets[i].alive) continue;
            bullets[i].x += bullets[i].dx;
            bullets[i].y += bullets[i].dy;
            if (bullets[i].x < 0 || bullets[i].x > 240 || bullets[i].y < 0 || bullets[i].y > 240) {
                bullets[i].alive = 0;
            }
        }

        // Spawn monsters
        if (HAL_GetTick() - last_spawn_time > 2000) {
            for (int i = 0; i < MAX_MONSTERS; i++) {
                if (!monsters[i].alive) {
                    uint16_t side = Random_U16(4);
                    if (side == 0)      { monsters[i].x = Random_U16(200) + 20; monsters[i].y = 10;  }
                    else if (side == 1) { monsters[i].x = Random_U16(200) + 20; monsters[i].y = 220; }
                    else if (side == 2) { monsters[i].x = 10;  monsters[i].y = Random_U16(180) + 30; }
                    else                { monsters[i].x = 220; monsters[i].y = Random_U16(180) + 30; }
                    monsters[i].alive = 1;
                    break;
                }
            }
            last_spawn_time = HAL_GetTick();
        }

        // Update monsters
        if (frame_counter % 3 == 0) {
            for (int i = 0; i < MAX_MONSTERS; i++) {
                if (!monsters[i].alive) continue;
                if (monsters[i].x < player_x) monsters[i].x += MONSTER_SPEED;
                if (monsters[i].x > player_x) monsters[i].x -= MONSTER_SPEED;
                if (monsters[i].y < player_y) monsters[i].y += MONSTER_SPEED;
                if (monsters[i].y > player_y) monsters[i].y -= MONSTER_SPEED;
            }
        }

        // Bullet hits monster
        for (int i = 0; i < MAX_BULLETS; i++) {
            if (!bullets[i].alive) continue;
            for (int j = 0; j < MAX_MONSTERS; j++) {
                if (!monsters[j].alive) continue;
                int16_t dx = bullets[i].x - monsters[j].x;
                int16_t dy = bullets[i].y - monsters[j].y;
                if (dx < 0) { dx = -dx; }
                if (dy < 0) { dy = -dy; }
                if (dx < 8 && dy < 8) {
                    bullets[i].alive = 0;
                    monsters[j].alive = 0;
                    kill_count++;

                    buzzer_tone(&buzzer_cfg, 2500, 40);
                    HAL_Delay(20);
                    buzzer_off(&buzzer_cfg);

                    PWM_SetDuty(&pwm_cfg, 100);
                    HAL_Delay(80);
                    PWM_SetDuty(&pwm_cfg, 0);
                }
            }
        }

        // Monster touches player
        uint32_t now = HAL_GetTick();
        if (now - last_damage_time1 > DAMAGE_COOLDOWN_MS) {
            for (int i = 0; i < MAX_MONSTERS; i++) {
                if (!monsters[i].alive) continue;
                int16_t dx = monsters[i].x - player_x;
                int16_t dy = monsters[i].y - player_y;
                if (dx < 0) { dx = -dx; }   
                if (dy < 0) { dy = -dy; }   
                if (dx < 10 && dy < 10) {
                    player_hp--;
                    last_damage_time1 = now;

                    buzzer_tone(&buzzer_cfg, 600, 80);
                    HAL_Delay(100);
                    buzzer_off(&buzzer_cfg);
                    
                    PWM_SetDuty(&pwm_cfg, 100);
                    HAL_Delay(120);
                    PWM_SetDuty(&pwm_cfg, 0);
                    break;
                }
            }
        }

        // Level 1 clear
        if (kill_count >= KILL_TO_WIN) {
            Victory_Fireworks();

            LCD_Fill_Buffer(0x0000);
            LCD_printString("LEVEL 1 CLEAR!", 0, 30, 1, 3);
            LCD_printString("10 enemies defeated", 0, 60, 1, 2);
            LCD_printString("Press BT3 to next", 0, 90, 1, 2);
            LCD_Refresh(&cfg0);

            buzzer_tone(&buzzer_cfg, 1000, 50); HAL_Delay(100);
            buzzer_tone(&buzzer_cfg, 1500, 50); HAL_Delay(100);
            buzzer_tone(&buzzer_cfg, 2000, 80); HAL_Delay(200);
            buzzer_off(&buzzer_cfg);

            while (1) {
                Input_Read();
                if (current_input.btn3_pressed) break;
            }
            break;   // Go to Level 2
        }

        // Level 1 failed
        if (player_hp <= 0) {
            LCD_Fill_Buffer(0x0000);
            LCD_printString("GAME OVER", 20, 30, 1, 3);
            char buf[32];
            sprintf(buf, "Kills: %d/10", kill_count);
            LCD_printString(buf, 15, 60, 1, 2);
            LCD_printString("BT3 to return", 10, 90, 1, 2);
            LCD_Refresh(&cfg0);

            buzzer_tone(&buzzer_cfg, 800, 80); HAL_Delay(150);
            buzzer_tone(&buzzer_cfg, 400, 100); HAL_Delay(200);
            buzzer_off(&buzzer_cfg);

            while (1) {
                Input_Read();
                if (current_input.btn3_pressed) break;
            }
            return MENU_STATE_HOME;
        }

        // Level 1 rendering
        LCD_Fill_Buffer(0x0000);
        
        // Draw forest background
        for (int y = 100; y < 240; y += 15) {
            for (int x = 0; x < 240; x += 20) {
                LCD_printString("^~", x, y, 3, 1); // grass
            }
        }
        for (int x = 20; x < 220; x += 40) {
            LCD_printString("|", x, 80, 3, 3); // tree trunk
            LCD_printString("Y", x, 75, 3, 3); // tree leaves
        }
        
        LCD_printString("FOREST LEVEL 1", 20, 2, 1, 2);
        char hp_str[16];  sprintf(hp_str, "HP: %d", player_hp);
        LCD_printString(hp_str, 5, 25, 3, 2); // green HP
        char kill_str[16]; sprintf(kill_str, "Kills: %d/10", kill_count);
        LCD_printString(kill_str, 100, 25, 5, 2); // orange kill count

        // Draw player (human)
        LCD_printString("O", player_x, player_y - 2, 3, 2); // head
        LCD_printString("|", player_x, player_y, 3, 2);     // body
        LCD_printString("/", player_x - 1, player_y + 1, 3, 2); // left arm
        LCD_printString("\\", player_x + 1, player_y + 1, 3, 2); // right arm
        LCD_printString("/", player_x, player_y + 2, 3, 2);     // left leg
        LCD_printString("\\", player_x + 1, player_y + 2, 3, 2); // right leg
        
        // Draw bullets
        for (int i = 0; i < MAX_BULLETS; i++) {
            if (bullets[i].alive) {
                LCD_printString(".", bullets[i].x, bullets[i].y, 6, 2); // yellow bullets
            }
        }
        
        // Draw enemies
        for (int i = 0; i < MAX_MONSTERS; i++) {
            if (monsters[i].alive) {
                int enemy_x = monsters[i].x;
                int enemy_y = monsters[i].y;
                LCD_printString("X", enemy_x, enemy_y - 1, 2, 2); // head
                LCD_printString("W", enemy_x, enemy_y, 2, 2);     // body
                LCD_printString("V", enemy_x, enemy_y + 1, 2, 2); // legs
            }
        }

        LCD_printString("BT2:Shoot", 5, 50, 1, 1);
        LCD_printString("BT3:Exit", 100, 50, 1, 1);
        LCD_Refresh(&cfg0);

        uint32_t frame_time = HAL_GetTick() - frame_start;
        if (frame_time < GAME3_FRAME_TIME_MS) HAL_Delay(GAME3_FRAME_TIME_MS - frame_time);
    }

    // ==================== Level 2 ====================
    Show_Intro_Level2();

    // Level 2 initialization
    bird_x = 60; bird_y = 120; bird_dy = 0;
    speed_level = 1;
    frame_counter2 = 0;
    player_hp2 = MAX_HP;
    last_damage_time2 = 0;
    for (int i = 0; i < MAX_PIPES; i++) pipes[i].alive = 0;
    uint8_t game_started = 0;

    game_start_time = HAL_GetTick();
    last_pipe_time  = HAL_GetTick();

    while (1) {
        uint32_t frame_start = HAL_GetTick();
        frame_counter2++;

        Input_Read();

        if (current_input.btn3_pressed) {
            exit_state = MENU_STATE_HOME;
            break;
        }

        if (!game_started) {
            LCD_Fill_Buffer(13);
            LCD_printString("LEVEL 2 - FLY!", 30, 5, 1, 2);
            LCD_printString("Press BT2 to start!", 25, 60, 1, 2);
            LCD_printString("<o>", bird_x - 1, bird_y, 5, 2);
            LCD_printString("BT3:Exit", 100, 100, 1, 1);
            LCD_Refresh(&cfg0);
            
            if (current_input.btn2_pressed) {
                game_started = 1;
                game_start_time = HAL_GetTick();
                last_pipe_time = HAL_GetTick();
                buzzer_tone(&buzzer_cfg, 1200, 80);
                HAL_Delay(100);
                buzzer_off(&buzzer_cfg);
            }
            uint32_t ft = HAL_GetTick() - frame_start;
            if (ft < GAME_FRAME_TIME_MS) HAL_Delay(GAME_FRAME_TIME_MS - ft);
            continue;
        }

        // Bird control
        if (current_input.btn2_pressed) {
            bird_dy = -5;
        }
        bird_dy += 1;
        if (bird_dy > 6) bird_dy = 6;
        bird_y += bird_dy;

        if (bird_y < SCREEN_TOP || bird_y > SCREEN_BOTTOM) {
            Game_Fail();
            return MENU_STATE_HOME;
        }

        // Pipe generation
        uint32_t pipe_interval = 1400 - (uint32_t)speed_level * 80;
        if (pipe_interval < 400) pipe_interval = 400;

        if (HAL_GetTick() - last_pipe_time > pipe_interval) {
            for (int i = 0; i < MAX_PIPES; i++) {
                if (!pipes[i].alive) {
                    pipes[i].x = 240;
                    uint16_t gap_range = (uint16_t)(SCREEN_BOTTOM - GAP_HALF - 10) - (uint16_t)(SCREEN_TOP + GAP_HALF + 10);
                    pipes[i].gap_y = (int16_t)(SCREEN_TOP + GAP_HALF + 10) + (int16_t)Random_U16(gap_range);
                    pipes[i].alive = 1;
                    break;
                }
            }
            last_pipe_time = HAL_GetTick();
        }

        // Pipe movement and collision
        int16_t pipe_speed = 2 + speed_level / 3;
        uint8_t crashed_this_frame = 0;

        for (int i = 0; i < MAX_PIPES; i++) {
            if (!pipes[i].alive) continue;
            pipes[i].x -= pipe_speed;

            if (pipes[i].x < -PIPE_WIDTH) {
                pipes[i].alive = 0;
                continue;
            }

            uint8_t x_overlap = (bird_x + 6 > pipes[i].x) && (bird_x - 6 < pipes[i].x + PIPE_WIDTH);
            if (x_overlap) {
                uint8_t in_gap = (bird_y - 6 >= pipes[i].gap_y - GAP_HALF) &&
                                 (bird_y + 6 <= pipes[i].gap_y + GAP_HALF);
                if (!in_gap) {
                    crashed_this_frame = 1;
                    break;
                }
            }
        }

        uint32_t now2 = HAL_GetTick();
        if (crashed_this_frame && (now2 - last_damage_time2 > DAMAGE_COOLDOWN_MS2)) {
            player_hp2--;
            last_damage_time2 = now2;

            buzzer_tone(&buzzer_cfg, 600, 80); HAL_Delay(100); buzzer_off(&buzzer_cfg);
            PWM_SetDuty(&pwm_cfg, 100); HAL_Delay(120); PWM_SetDuty(&pwm_cfg, 0);

            if (player_hp2 == 0) {
                Game_Fail();
                return MENU_STATE_HOME;
            }
        }

        // Difficulty increase
        uint32_t elapsed = HAL_GetTick() - game_start_time;
        uint8_t target_level = (uint8_t)(elapsed / 5000) + 1;
        if (target_level > MAX_SPEED_LEVEL) target_level = MAX_SPEED_LEVEL;
        if (target_level > speed_level) speed_level = target_level;

        // Level 2 clear
        if (elapsed > SURVIVAL_TIME_SEC * 1000UL) {
            LCD_Fill_Buffer(0x0000);
            LCD_printString("LEVEL 2 CLEAR!", 10, 20, 1, 3);
            LCD_printString("25 seconds survived!", 5, 50, 1, 2);
            LCD_printString("Press BT3 to next", 5, 80, 1, 2);
            LCD_Refresh(&cfg0);

            buzzer_tone(&buzzer_cfg, 1500, 80); HAL_Delay(100);
            buzzer_tone(&buzzer_cfg, 2000, 80); HAL_Delay(100);
            buzzer_tone(&buzzer_cfg, 2500, 120); HAL_Delay(200);
            buzzer_off(&buzzer_cfg);

            while (1) {
                Input_Read();
                if (current_input.btn3_pressed) break;
            }
            break;
        }

        // Level 2 rendering
        LCD_Fill_Buffer(13); // Use grey for mine background (color index 13)
        
        LCD_printString("LEVEL 2 - MINE", 25, 5, 1, 2);

        char hp_buf[16]; sprintf(hp_buf, "HP: %d", player_hp2);
        LCD_printString(hp_buf, 5, 25, 3, 2); // green HP

        uint32_t remaining = SURVIVAL_TIME_SEC - (elapsed / 1000);
        char time_buf[24];
        sprintf(time_buf, "Time: %lus", remaining);
        LCD_printString(time_buf, 100, 25, 6, 2); // yellow time

        // Draw mine decorations
        for (int x = 0; x < 240; x += 40) {
            LCD_printString("||", x, SCREEN_TOP, 12, 2);     // top supports
            LCD_printString("||", x, SCREEN_BOTTOM - 10, 12, 2); // bottom supports
        }
        for (int y = SCREEN_TOP; y < SCREEN_BOTTOM; y += 30) {
            LCD_printString("=", 0, y, 12, 1);
            LCD_printString("=", 235, y, 12, 1);
        }

        // Draw pipes (optimized display)
        for (int i = 0; i < MAX_PIPES; i++) {
            if (!pipes[i].alive) continue;
            int16_t gap_top = pipes[i].gap_y - GAP_HALF;
            int16_t gap_bottom = pipes[i].gap_y + GAP_HALF;

            // Top pipe
            LCD_printString("+---+", pipes[i].x - 3, gap_top - 20, 12, 2); // top cap
            for (int16_t py = gap_top - 14; py > SCREEN_TOP; py -= 12) {
                LCD_printString("|   |", pipes[i].x - 3, py, 12, 2); // pipe body
            }
            LCD_printString("+---+", pipes[i].x - 3, SCREEN_TOP, 12, 2); // bottom cap

            // Bottom pipe
            LCD_printString("+---+", pipes[i].x - 3, gap_bottom, 12, 2); // top cap
            for (int16_t py = gap_bottom + 8; py < SCREEN_BOTTOM - 6; py += 12) {
                LCD_printString("|   |", pipes[i].x - 3, py, 12, 2); // pipe body
            }
            LCD_printString("+---+", pipes[i].x - 3, SCREEN_BOTTOM - 6, 12, 2); // bottom cap
        }

        // Draw bird (optimized display)
        LCD_printString("(", bird_x - 3, bird_y - 1, 5, 2); // left wing
        LCD_printString("0", bird_x - 1, bird_y, 5, 2);     // body
        LCD_printString(")", bird_x + 2, bird_y - 1, 5, 2); // right wing
        LCD_printString("^", bird_x, bird_y - 2, 5, 1);     // top feather
        
        LCD_printString("BT2:Fly", 5, 50, 1, 1);
        LCD_printString("BT3:Exit", 100, 50, 1, 1);

        LCD_Refresh(&cfg0);

        uint32_t ft = HAL_GetTick() - frame_start;
        if (ft < GAME_FRAME_TIME_MS) HAL_Delay(GAME_FRAME_TIME_MS - ft);
    }

    // ==================== Level 3 ====================
    L3_Show_Intro();

    // Level 3 initialization
    l3_px  = 120; 
    l3_py  = 200;   
    l3_php = L3_PLAYER_HP;

    l3_bx  = 120; 
    l3_by  = 60;    
    l3_bhp = L3_BOSS_HP;
    l3_bdx = L3_BOSS_SPEED;
    l3_bdy = 0;

    for (int i = 0; i < L3_MAX_P_BULLETS; i++) l3_pbullets[i].alive = 0;
    for (int i = 0; i < L3_MAX_B_BULLETS; i++) l3_bbullets[i].alive = 0;

    l3_last_shoot_ms  = 0;
    l3_boss_shoot_ms  = 0;
    l3_last_dmg_ms    = 0;
    l3_frame          = 0;
    l3_boss_shoot_count = 0;

    while (1) {
        uint32_t frame_start = HAL_GetTick();
        l3_frame++;

        Input_Read();
        Joystick_Read(&joystick_cfg, &joystick_data);

        if (current_input.btn3_pressed) {
            return MENU_STATE_HOME;
        }

        // ---- Player movement ----
        switch (joystick_data.direction) {
            case N:  l3_py -= L3_PLAYER_SPEED; break;
            case S:  l3_py += L3_PLAYER_SPEED; break;
            case W:  l3_px -= L3_PLAYER_SPEED; break;
            case E:  l3_px += L3_PLAYER_SPEED; break;
            case NW: l3_px -= L3_PLAYER_SPEED; l3_py -= L3_PLAYER_SPEED; break;
            case NE: l3_px += L3_PLAYER_SPEED; l3_py -= L3_PLAYER_SPEED; break;
            case SW: l3_px -= L3_PLAYER_SPEED; l3_py += L3_PLAYER_SPEED; break;
            case SE: l3_px += L3_PLAYER_SPEED; l3_py += L3_PLAYER_SPEED; break;
            default: break;
        }

        // Boundary constraints
        if (l3_px < L3_SCR_LEFT)   l3_px = L3_SCR_LEFT;
        if (l3_px > L3_SCR_RIGHT)  l3_px = L3_SCR_RIGHT;
        if (l3_py < L3_SCR_TOP)    l3_py = L3_SCR_TOP;
        if (l3_py > L3_SCR_BOTTOM) l3_py = L3_SCR_BOTTOM;

        // ---- BT2 Shoot ----
        uint32_t now = HAL_GetTick();

        if (current_input.btn2_pressed) {
            if (now - l3_last_shoot_ms > L3_SHOOT_COOLDOWN_MS) {
                for (int i = 0; i < L3_MAX_P_BULLETS; i++) {
                    if (l3_pbullets[i].alive) continue;
                    l3_pbullets[i].x         = l3_px;
                    l3_pbullets[i].y         = l3_py;
                    l3_pbullets[i].alive     = 1;
                    l3_pbullets[i].dx        = 0;
                    l3_pbullets[i].dy        = -L3_P_BULLET_SPEED;

                    l3_last_shoot_ms = now;

                    buzzer_tone(&buzzer_cfg, 1800, 15);
                    HAL_Delay(8);
                    buzzer_off(&buzzer_cfg);
                    break;
                }
            }
        }

        // ---- Update player bullets ----
        for (int i = 0; i < L3_MAX_P_BULLETS; i++) {
            if (!l3_pbullets[i].alive) continue;
            l3_pbullets[i].x += l3_pbullets[i].dx;
            l3_pbullets[i].y += l3_pbullets[i].dy;
            if (l3_pbullets[i].x < 0 || l3_pbullets[i].x > 240 ||
                l3_pbullets[i].y < 0 || l3_pbullets[i].y > 240) {
                l3_pbullets[i].alive = 0;
            }
        }

        // ---- Player bullet hits Boss ----
        for (int i = 0; i < L3_MAX_P_BULLETS; i++) {
            if (!l3_pbullets[i].alive) continue;
            int16_t dx = l3_pbullets[i].x - l3_bx;
            int16_t dy = l3_pbullets[i].y - l3_by;
            if (dx < 0) dx = -dx;
            if (dy < 0) dy = -dy;
            if (dx < L3_BOSS_SIZE && dy < L3_BOSS_SIZE) {
                l3_pbullets[i].alive = 0;
                l3_bhp -= 1;
                buzzer_tone(&buzzer_cfg, 2200, 20); HAL_Delay(10); buzzer_off(&buzzer_cfg);
                PWM_SetDuty(&pwm_cfg, 60); HAL_Delay(40); PWM_SetDuty(&pwm_cfg, 0);
            }
        }

        // ---- Boss movement ----
        if (l3_frame % 2 == 0) {
            l3_bx += l3_bdx;
            if (l3_bx < 40)  { l3_bx = 40;  l3_bdx =  L3_BOSS_SPEED; }
            if (l3_bx > 200) { l3_bx = 200; l3_bdx = -L3_BOSS_SPEED; }

            if (l3_frame % 40 == 0) {
                if (l3_by < l3_py - 40) l3_by += 1;
                if (l3_by > 80)         l3_by -= 1;
            }
        }

        // ---- Boss shoot ----
        if (now - l3_boss_shoot_ms > L3_BOSS_SHOOT_CD_MS) {
            l3_boss_shoot();
            l3_boss_shoot_ms = now;
        }

        // ---- Update Boss bullets ----
        for (int i = 0; i < L3_MAX_B_BULLETS; i++) {
            if (!l3_bbullets[i].alive) continue;
            l3_bbullets[i].x += l3_bbullets[i].dx;
            l3_bbullets[i].y += l3_bbullets[i].dy;
            if (l3_bbullets[i].x < 0 || l3_bbullets[i].x > 240 ||
                l3_bbullets[i].y < 0 || l3_bbullets[i].y > 240) {
                l3_bbullets[i].alive = 0;
            }
        }

        // ---- Bullet cancelation ----
        for (int i = 0; i < L3_MAX_P_BULLETS; i++) {
            if (!l3_pbullets[i].alive) continue;
            for (int j = 0; j < L3_MAX_B_BULLETS; j++) {
                if (!l3_bbullets[j].alive) continue;
                int16_t dx = l3_pbullets[i].x - l3_bbullets[j].x;
                int16_t dy = l3_pbullets[i].y - l3_bbullets[j].y;
                if (dx < 0) dx = -dx;
                if (dy < 0) dy = -dy;
                if (dx < 6 && dy < 6) {
                    l3_pbullets[i].alive = 0;
                    l3_bbullets[j].alive = 0;
                    break;
                }
            }
        }

        // ---- Boss bullet hits player ----
        now = HAL_GetTick();
        if (now - l3_last_dmg_ms > L3_PLAYER_DMG_CD_MS) {
            for (int i = 0; i < L3_MAX_B_BULLETS; i++) {
                if (!l3_bbullets[i].alive) continue;
                int16_t dx = l3_bbullets[i].x - l3_px;
                int16_t dy = l3_bbullets[i].y - l3_py;
                if (dx < 0) dx = -dx;
                if (dy < 0) dy = -dy;
                if (dx < 8 && dy < 8) {
                    l3_bbullets[i].alive = 0;
                    l3_php--;
                    l3_last_dmg_ms = now;
                    buzzer_tone(&buzzer_cfg, 500, 80); HAL_Delay(90); buzzer_off(&buzzer_cfg);
                    PWM_SetDuty(&pwm_cfg, 100); HAL_Delay(80); PWM_SetDuty(&pwm_cfg, 0);
                    break;
                }
            }
        }

        // ---- Win/Lose check ----
        if (l3_bhp <= 0) {
            L3_Victory();
            return MENU_STATE_HOME;
        }
        if (l3_php <= 0) {
            L3_GameOver();
            return MENU_STATE_HOME;
        }

        // ---- Rendering ----
        LCD_Fill_Buffer(0x0000);
        
        // Draw lava background
        for (int y = 150; y < 240; y += 10) {
            for (int x = 0; x < 240; x += 15) {
                LCD_printString("~", x, y, 5, 1); // lava
            }
        }
        for (int y = 160; y < 230; y += 20) {
            for (int x = 5; x < 235; x += 25) {
                LCD_printString("^", x, y, 2, 1); // fire
            }
        }
        for (int x = 0; x < 240; x += 30) {
            LCD_printString("|", x, 140, 13, 2); // rocks
        }
        
        LCD_printString("LEVEL 3 - BOSS", 25, 2, 1, 2);

        char buf[32];
        sprintf(buf, "HP: %d", l3_php);
        LCD_printString(buf, 5, 25, 3, 2); // green HP

        sprintf(buf, "BOSS: %d", l3_bhp);
        LCD_printString(buf, 120, 25, 2, 2); // red BOSS HP

        // Draw BOSS (detailed model)
        LCD_printString("  ###  ", l3_bx - 15, l3_by - 12, 2, 2); // head
        LCD_printString(" #XXX# ", l3_bx - 15, l3_by - 8,  2, 2); // eyes
        LCD_printString("#####", l3_bx - 12, l3_by - 4,  2, 2); // chin
        LCD_printString("#   #", l3_bx - 12, l3_by,      2, 2); // body
        LCD_printString("#XXX#", l3_bx - 12, l3_by + 4,  2, 2); // body
        LCD_printString("#   #", l3_bx - 12, l3_by + 8,  2, 2); // body
        LCD_printString("#####", l3_bx - 12, l3_by + 12, 2, 2); // bottom
        LCD_printString(" | | ", l3_bx - 8, l3_by + 16,  2, 2); // legs

        // Draw player (human)
        LCD_printString("O", l3_px, l3_py - 2, 3, 2); // head
        LCD_printString("|", l3_px, l3_py, 3, 2);     // body
        LCD_printString("/", l3_px - 1, l3_py + 1, 3, 2); // left arm
        LCD_printString("\\", l3_px + 1, l3_py + 1, 3, 2); // right arm
        LCD_printString("/", l3_px, l3_py + 2, 3, 2);     // left leg
        LCD_printString("\\", l3_px + 1, l3_py + 2, 3, 2); // right leg

        // Normal bullets
        for (int i = 0; i < L3_MAX_P_BULLETS; i++) {
            if (l3_pbullets[i].alive) {
                LCD_printString(".", l3_pbullets[i].x, l3_pbullets[i].y, 6, 2); // yellow bullets
            }
        }

        // Boss bullets
        for (int i = 0; i < L3_MAX_B_BULLETS; i++) {
            if (l3_bbullets[i].alive) {
                LCD_printString("*", l3_bbullets[i].x, l3_bbullets[i].y, 7, 2); // pink BOSS bullets
            }
        }

        LCD_printString("BT2:Shoot", 2, 80, 1, 1);
        LCD_printString("BT3:Exit", 100, 80, 1, 1);
        LCD_Refresh(&cfg0);

        // Frame rate control
        uint32_t ft = HAL_GetTick() - frame_start;
        if (ft < L3_FRAME_MS) HAL_Delay(L3_FRAME_MS - ft);
    }

    PWM_SetDuty(&pwm_cfg, 30);
    return exit_state;
}