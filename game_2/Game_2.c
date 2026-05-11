#include "Game_2.h"
#include "InputHandler.h"
#include "Menu.h"
#include "LCD.h"
#include "Buzzer.h"
#include "stm32l4xx_hal.h"
#include "adc.h"
#include <stdio.h>
#include <math.h>

extern ST7789V2_cfg_t cfg0;
extern Buzzer_cfg_t buzzer_cfg;
extern ADC_HandleTypeDef hadc1;

#define MAP_SIZE 20
#define GAME_FRAME_MS 33
#define MAX_ENEMIES 2
#define SPAWN_INTERVAL_MS 10000
#define DAMAGE_INTERVAL_MS 1000
#define ENEMY_HP 10
#define PLAYER_HP_MAX 20
#define BULLET_DAMAGE 1
#define HEAL_AMOUNT 4
#define KILLS_TO_WIN 5
#define ENEMY_SPEED 0.15f
#define PLAYER_SPEED 0.08f
#define ROTATION_SPEED 0.05f
#define MELEE_RANGE 2.0f        
#define MELEE_COOLDOWN_MS 1000  
#define MELEE_DISPLAY_MS 1000   
#define KNIFE_OFFSET_Y 30       

typedef struct {
    float x;
    float y;
    float angle;
    int hp;
    int kills;
} Player;

typedef struct {
    float x;
    float y;
    int hp;
    int active;
    int flash_count;
} Enemy;

typedef struct {
    float x;
    float y;
    float dx;
    float dy;
    int active;
} Bullet;

typedef struct {
    float x;
    float y;
    int active;
} Pickup;

static Player player;
static Enemy enemies[MAX_ENEMIES];
static Bullet bullets[5];
static Pickup pickups[5];
static uint8_t map[MAP_SIZE][MAP_SIZE];
static uint32_t last_spawn_time;
static uint32_t last_damage_time;
static uint8_t red_flash;
static uint32_t red_flash_end;
static uint8_t is_defending;
static uint32_t buzzer_stop_time;
static uint8_t buzzer_active;
static uint8_t crosshair_hit;
static uint8_t melee_active;        
static uint32_t melee_end_time;     
static uint32_t last_melee_time;    

static uint16_t read_adc(uint32_t channel) {
    ADC_ChannelConfTypeDef sConfig = {0};
    sConfig.Channel = channel;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_6CYCLES_5;
    sConfig.SingleDiff = ADC_SINGLE_ENDED;
    sConfig.OffsetNumber = ADC_OFFSET_NONE;
    sConfig.Offset = 0;
    HAL_ADC_ConfigChannel(&hadc1, &sConfig);
    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, 10);
    uint16_t val = HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);
    return val;
}

static void init_map(void) {
    for (int y = 0; y < MAP_SIZE; y++) {
        for (int x = 0; x < MAP_SIZE; x++) {
            // 只有四周的围墙
            if (x == 0 || x == MAP_SIZE - 1 || y == 0 || y == MAP_SIZE - 1) {
                map[y][x] = 1;
            } else {
                map[y][x] = 0;
            }
        }
    }
}

static void init_player(void) {
    player.x = 3.0f;
    player.y = 3.0f;
    player.angle = 0.0f;
    player.hp = PLAYER_HP_MAX;
    player.kills = 0;
}

static void init_enemies(void) {
    for (int i = 0; i < MAX_ENEMIES; i++) {
        enemies[i].active = 0;
        enemies[i].flash_count = 0;
    }
}

static void init_bullets(void) {
    for (int i = 0; i < 5; i++) {
        bullets[i].active = 0;
    }
}

static void init_pickups(void) {
    for (int i = 0; i < 5; i++) {
        pickups[i].active = 0;
    }
}

static void spawn_enemy(void) {
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!enemies[i].active) {
            float ex, ey;
            int attempts = 0;
            do {
               
                ex = 2.0f + (float)(rand() % 16);
                ey = 2.0f + (float)(rand() % 16);
                attempts++;
            } while ((map[(int)ey][(int)ex] != 0 || 
                     (fabs(ex - player.x) < 5.0f && fabs(ey - player.y) < 5.0f)) &&
                     attempts < 100);
            
            // 强制使用绝对安全位置
            if (attempts >= 100) {
                ex = 10.0f;
                ey = 10.0f;
            }
            
            enemies[i].x = ex;
            enemies[i].y = ey;
            enemies[i].hp = ENEMY_HP;
            enemies[i].active = 1;
            enemies[i].flash_count = 0;
            break;
        }
    }
}

static void play_sound(uint32_t freq, uint8_t volume, uint32_t duration) {
    buzzer_tone(&buzzer_cfg, freq, volume);
    buzzer_active = 1;
    buzzer_stop_time = HAL_GetTick() + duration;
}

static void update_buzzer(void) {
    if (buzzer_active && HAL_GetTick() > buzzer_stop_time) {
        buzzer_off(&buzzer_cfg);
        buzzer_active = 0;
    }
}

static void shoot(void) {
    play_sound(800, 30, 80);
    
    // Spawn bullet
    for (int i = 0; i < 5; i++) {
        if (!bullets[i].active) {
            bullets[i].x = player.x + cosf(player.angle) * 0.5f;
            bullets[i].y = player.y + sinf(player.angle) * 0.5f;
            bullets[i].dx = cosf(player.angle) * 0.5f;
            bullets[i].dy = sinf(player.angle) * 0.5f;
            bullets[i].active = 1;
            break;
        }
    }
}

static void update_bullets(void) {
    for (int i = 0; i < 5; i++) {
        if (bullets[i].active) {
           
            for (int step = 0; step < 3; step++) {
                bullets[i].x += bullets[i].dx / 3.0f;
                bullets[i].y += bullets[i].dy / 3.0f;
                
                // Check enemy collision
                for (int j = 0; j < MAX_ENEMIES; j++) {
                    if (enemies[j].active && enemies[j].hp > 0) {
                        float dx = bullets[i].x - enemies[j].x;
                        float dy = bullets[i].y - enemies[j].y;
                        if (sqrtf(dx * dx + dy * dy) < 0.7f) {
                            enemies[j].hp -= BULLET_DAMAGE;
                            bullets[i].active = 0;
                            if (enemies[j].hp <= 0) {
                                enemies[j].flash_count = 4;
                            }
                            goto next_bullet;
                        }
                    }
                }
                
                // Check wall collision
                if (map[(int)bullets[i].y][(int)bullets[i].x] != 0 ||
                    bullets[i].x < 0 || bullets[i].x >= MAP_SIZE ||
                    bullets[i].y < 0 || bullets[i].y >= MAP_SIZE) {
                    bullets[i].active = 0;
                    goto next_bullet;
                }
            }
        }
next_bullet:;
    }
}

static void draw_bullets(void) {
    for (int i = 0; i < 5; i++) {
        if (bullets[i].active) {
            float dx = bullets[i].x - player.x;
            float dy = bullets[i].y - player.y;
            float dist = sqrtf(dx * dx + dy * dy);
            float angle_to = atan2f(dy, dx);
            float angle_diff = angle_to - player.angle;
            while (angle_diff > M_PI) angle_diff -= 2 * M_PI;
            while (angle_diff < -M_PI) angle_diff += 2 * M_PI;
            if (fabs(angle_diff) < 0.8f && dist < 18.0f) {
                int screen_x = (int)((0.5f + angle_diff / 1.6f) * 240.0f);
                int size = (int)(20.0f / dist);
                int screen_y = 140 - size / 2;
                if (size >= 2 && screen_x > -50 && screen_x < 290) {
                    int screen_size = size > 20 ? 20 : size;
                    for (int sy = 0; sy < screen_size; sy++) {
                        for (int sx = 0; sx < screen_size; sx++) {
                            int px = screen_x - screen_size / 2 + sx;
                            int py = screen_y + sy;
                            if (px >= 0 && px < 240 && py >= 0 && py < 280) {
                                LCD_Set_Pixel(px, py, 4); // Yellow
                            }
                        }
                    }
                }
            }
        }
    }
}

static void spawn_pickup(float x, float y) {
    for (int i = 0; i < 5; i++) {
        if (!pickups[i].active) {
            pickups[i].x = x;
            pickups[i].y = y;
            pickups[i].active = 1;
            break;
        }
    }
}

static void draw_3d(void) {
    for (int x = 0; x < 240; x++) {
        float ray_angle = player.angle - 0.5f + ((float)x / 240.0f);
        float ray_x = cosf(ray_angle);
        float ray_y = sinf(ray_angle);
        float distance = 0.0f;
        int hit = 0;
        while (!hit && distance < 18.0f) {
            distance += 0.1f;
            int map_x = (int)(player.x + ray_x * distance);
            int map_y = (int)(player.y + ray_y * distance);
            if (map_x >= 0 && map_x < MAP_SIZE && map_y >= 0 && map_y < MAP_SIZE) {
                if (map[map_y][map_x] == 1) {
                    hit = 1;
                }
            } else {
                hit = 1;
            }
        }
        float ceiling = (float)(140 - (int)(150 / distance));
        float floor = 280.0f - ceiling;
        for (int y = 0; y < 280; y++) {
            if (y < ceiling) {
                LCD_Set_Pixel(x, y, 0);
            } else if (y < floor) {
                int brightness = (int)(255.0f / (distance + 0.5f));
                LCD_Set_Pixel(x, y, (brightness > 15) ? 15 : brightness);
            } else {
                LCD_Set_Pixel(x, y, 8);
            }
        }
    }
}

static void draw_crosshair(void) {
    int cx = 120;
    int cy = 140;
    
    // Draw crosshair
    for (int i = -5; i <= 5; i++) {
        if (cx + i >= 0 && cx + i < 240) {
            LCD_Set_Pixel(cx + i, cy, 4);  // Yellow horizontal
        }
        if (cy + i >= 0 && cy + i < 280) {
            LCD_Set_Pixel(cx, cy + i, 4);  // Yellow vertical
        }
    }
    
    // If crosshair is on enemy, draw red center
    if (crosshair_hit) {
        LCD_Set_Pixel(cx, cy, 2);
        LCD_Set_Pixel(cx - 1, cy, 2);
        LCD_Set_Pixel(cx + 1, cy, 2);
        LCD_Set_Pixel(cx, cy - 1, 2);
        LCD_Set_Pixel(cx, cy + 1, 2);
    }
}

static void draw_sprites(void) {
    crosshair_hit = 0;
    
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (enemies[i].active) {
            float dx = enemies[i].x - player.x;
            float dy = enemies[i].y - player.y;
            float dist = sqrtf(dx * dx + dy * dy);
            float angle_to = atan2f(dy, dx);
            float angle_diff = angle_to - player.angle;
            while (angle_diff > M_PI) angle_diff -= 2 * M_PI;
            while (angle_diff < -M_PI) angle_diff += 2 * M_PI;
            
            if (fabs(angle_diff) < 0.8f && dist < 18.0f) {
                int screen_x = (int)((0.5f + angle_diff / 1.6f) * 240.0f);
                int size = (int)(300.0f / dist);
                int screen_y = 140 - size / 2;
                
                // Check if crosshair is on enemy
                if (screen_x - size/2 <= 120 && 120 <= screen_x + size/2 &&
                    screen_y <= 140 && 140 <= screen_y + size) {
                    crosshair_hit = 1;
                }
                
                // Skip drawing if flashing (for death animation)
                if (enemies[i].flash_count > 0) {
                    if (enemies[i].flash_count % 2 == 0) {
                        continue;
                    }
                }
                
                if (size > 8 && screen_x > -50 && screen_x < 290) {
                    int screen_size = size > 200 ? 200 : size;
                    for (int sy = 0; sy < screen_size; sy++) {
                        for (int sx = 0; sx < screen_size; sx++) {
                            int px = screen_x - screen_size / 2 + sx;
                            int py = screen_y + sy;
                            if (px >= 0 && px < 240 && py >= 0 && py < 280) {
                                if (sx < screen_size * 0.1f || sx > screen_size * 0.9f || 
                                    sy < screen_size * 0.1f || sy > screen_size * 0.9f) {
                                    LCD_Set_Pixel(px, py, 2);
                                } else if ((sy > screen_size * 0.2f && sy < screen_size * 0.35f && 
                                            sx > screen_size * 0.25f && sx < screen_size * 0.4f) ||
                                           (sy > screen_size * 0.2f && sy < screen_size * 0.35f && 
                                            sx > screen_size * 0.6f && sx < screen_size * 0.75f)) {
                                    LCD_Set_Pixel(px, py, 1);
                                } else if (sy > screen_size * 0.5f && sy < screen_size * 0.65f && 
                                           sx > screen_size * 0.2f && sx < screen_size * 0.8f) {
                                    LCD_Set_Pixel(px, py, 1);
                                } else {
                                    LCD_Set_Pixel(px, py, 2);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    
    for (int i = 0; i < 5; i++) {
        if (pickups[i].active) {
            float dx = pickups[i].x - player.x;
            float dy = pickups[i].y - player.y;
            float dist = sqrtf(dx * dx + dy * dy);
            float angle_to = atan2f(dy, dx);
            float angle_diff = angle_to - player.angle;
            while (angle_diff > M_PI) angle_diff -= 2 * M_PI;
            while (angle_diff < -M_PI) angle_diff += 2 * M_PI;
            if (fabs(angle_diff) < 0.8f && dist < 12.0f) {
                int screen_x = (int)((0.5f + angle_diff / 1.6f) * 240.0f);
                int size = (int)(150.0f / dist);
                int screen_y = 140 - size / 2;
                if (size > 10 && screen_x > -50 && screen_x < 290) {
                    int screen_size = size > 100 ? 100 : size;
                    for (int sy = 0; sy < screen_size; sy++) {
                        for (int sx = 0; sx < screen_size; sx++) {
                            int px = screen_x - screen_size / 2 + sx;
                            int py = screen_y + sy;
                            if (px >= 0 && px < 240 && py >= 0 && py < 280) {
                                if ((sy > screen_size * 0.2f && sy < screen_size * 0.8f && 
                                     sx > screen_size * 0.4f && sx < screen_size * 0.6f) ||
                                    (sx > screen_size * 0.2f && sx < screen_size * 0.8f && 
                                     sy > screen_size * 0.4f && sy < screen_size * 0.6f)) {
                                    LCD_Set_Pixel(px, py, 3);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

/* Gun bitmap: 82x60 pixel-art pistol, generated from reference image */
static const uint8_t gun_bitmap[60][11] = {
    { 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x16, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x40, 0x0E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x88, 0x00, 0x7C, 0x01, 0xE0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x80, 0x4F, 0xE0, 0x39, 0xE0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x80, 0x0F, 0x80, 0x05, 0xF8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x80, 0x07, 0x40, 0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x80, 0x00, 0x0C, 0x00, 0x7F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x80, 0x00, 0x00, 0x20, 0xFF, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0xC0, 0x00, 0x00, 0x01, 0xED, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x38, 0x00, 0x00, 0x01, 0xC9, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x03, 0x00, 0x00, 0x00, 0xCF, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x70, 0x00, 0x00, 0xFF, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x60, 0x0C, 0x00, 0x00, 0xFF, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x3C, 0x11, 0xC0, 0x00, 0x5F, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x0E, 0x01, 0x38, 0x00, 0x79, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x03, 0x00, 0x67, 0x00, 0x71, 0xE0, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x01, 0xE4, 0x50, 0xE0, 0x71, 0xE0, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x78, 0x63, 0x9E, 0x7F, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0xFE, 0x3B, 0xF7, 0xBF, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0xFB, 0x33, 0xFC, 0xDF, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0xF1, 0xBF, 0xFF, 0xEF, 0xD8, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0xF1, 0x7F, 0xFF, 0xF0, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0xE1, 0x5F, 0xFB, 0xBF, 0xF4, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0xE1, 0x5C, 0x1F, 0x7F, 0xE0, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0xE1, 0x58, 0x01, 0xFF, 0xC0, 0x80, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0xE0, 0xD8, 0x00, 0x3F, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0xB3, 0xB0, 0x00, 0x00, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x79, 0x20, 0x00, 0x00, 0x04, 0x20, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x0E, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x10, 0x80, 0x40, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x10, 0x00, 0xE0, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x24, 0x01, 0xFE, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x21, 0xFF, 0xFF, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x20, 0x0F, 0xFD, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x20, 0x03, 0xFD, 0xC0, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00 },
    { 0x00, 0x20, 0x01, 0xDD, 0xC0, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x08, 0xFF, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x7E, 0xC0, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00 },
    { 0x00, 0x08, 0x00, 0x4E, 0xC0, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00 },
    { 0x00, 0x12, 0x00, 0x7E, 0xE0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x01, 0x00, 0xD7, 0x60, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x64, 0xE7, 0x70, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x0F, 0xEF, 0x70, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x01, 0xDF, 0xB0, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00 },
    { 0x00, 0x10, 0x00, 0x77, 0xB8, 0x00, 0x00, 0x00, 0xC0, 0x00, 0x00 },
    { 0x00, 0x08, 0x02, 0x2F, 0xBC, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00 },
    { 0x00, 0x04, 0x00, 0x3F, 0xFE, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00 },
    { 0x00, 0x06, 0x00, 0x33, 0xFF, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00 },
    { 0x00, 0x08, 0xC8, 0x3F, 0xDF, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x08, 0x22, 0x77, 0xDF, 0xC0, 0x00, 0x00, 0x00, 0x80, 0x00 },
    { 0x00, 0x08, 0x04, 0xE3, 0xDF, 0xA0, 0x00, 0x00, 0x00, 0x40, 0x00 },
    { 0x00, 0x08, 0x11, 0xFF, 0xEF, 0x80, 0x00, 0x00, 0x00, 0x10, 0x00 },
    { 0x00, 0x04, 0x04, 0xDD, 0xEF, 0x98, 0x00, 0x00, 0x00, 0x08, 0x00 },
    { 0x00, 0x02, 0x04, 0xF3, 0xEF, 0x80, 0x00, 0x00, 0x00, 0x04, 0x00 },
    { 0x00, 0x01, 0x24, 0xFF, 0xEF, 0x80, 0x09, 0x00, 0x00, 0x01, 0x00 },
    { 0x00, 0x00, 0xC9, 0xFD, 0xEF, 0x80, 0x14, 0x00, 0x00, 0x00, 0x80 }
};


#ifndef GUN_OFFSET_Y
#define GUN_OFFSET_Y 30
#endif

static void draw_gun(void) {
    const int start_x = 158;
    const int start_y = 220 - GUN_OFFSET_Y;
    for (int y = 0; y < 60; y++) {
        for (int x = 0; x < 82; x++) {
            int byte_idx = x >> 3;
            int bit_idx = 7 - (x & 7);
            if (gun_bitmap[y][byte_idx] & (1 << bit_idx)) {
                LCD_Set_Pixel(start_x + x, start_y + y, 1);
            }
        }
    }
}

/* Knife bitmap (left hand): 55x60, mirrored from reference image */
static const uint8_t knife_bitmap[60][7] = {
    { 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x88, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x20, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x04, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x08, 0x00, 0x80, 0x00, 0x00, 0x00 },
    { 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x60, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x70, 0x00, 0x00, 0x00 },
    { 0x00, 0x01, 0x00, 0xF8, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x01, 0xF7, 0xC0, 0x00, 0x00 },
    { 0x00, 0x00, 0xBF, 0xFC, 0x18, 0x00, 0x00 },
    { 0x00, 0x00, 0x07, 0x83, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x0E, 0x00, 0xB8, 0x80, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x02, 0x40, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20 },
    { 0x00, 0x00, 0x04, 0x00, 0x00, 0x10, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }
};

static void draw_knife(void) {
    if (melee_active) {
        uint32_t now = HAL_GetTick();
        if (now < melee_end_time) {
            const int start_x = 0;
            const int start_y = 220 - KNIFE_OFFSET_Y;
            for (int y = 0; y < 60; y++) {
                for (int x = 0; x < 55; x++) {
                    int byte_idx = x >> 3;
                    int bit_idx = 7 - (x & 7);
                    if (knife_bitmap[y][byte_idx] & (1 << bit_idx)) {
                        LCD_Set_Pixel(start_x + x, start_y + y, 1);
                    }
                }
            }
        } else {
            melee_active = 0;
        }
    }
}

static void draw_shield(void) {
    if (is_defending) {
        // Simple classic shield: top rounded, bottom pointed
        int center_x = 50;
        int top_y = 130;
        int bottom_y = 240;
        
        for (int y = top_y; y < bottom_y; y++) {
            // Calculate how far along the height we are (0 to 1)
            float t = (float)(y - top_y) / (bottom_y - top_y);
            
            // Calculate width at this y
            float width;
            
            if (t < 0.15f) {
                // Top part: curve outward
                width = 18 + 5 * cosf(t * 3.1416f * 3);
            } else if (t < 0.7f) {
                // Middle part: full width
                width = 23;
            } else {
                // Bottom part: V shape, narrow to a point
                float v_t = (t - 0.7f) / 0.3f;
                width = 23 * (1.0f - v_t);
            }
            
            // Draw this row
            int start_x = center_x - (int)width;
            int end_x = center_x + (int)width;
            for (int x = start_x; x <= end_x; x++) {
                if (x >= 0 && x < 240) {
                    LCD_Set_Pixel(x, y, 4);  // Yellow
                }
            }
        }
    }
}

static void draw_ui(void) {
    char hp_str[20];
    sprintf(hp_str, "%d/%d", player.hp, PLAYER_HP_MAX);
    LCD_printString(hp_str, 5, 5, 1, 2);
    char kill_str[20];
    sprintf(kill_str, "KILLS:%d/%d", player.kills, KILLS_TO_WIN);
    LCD_printString(kill_str, 5, 25, 1, 1);
}

static void update_player(uint32_t frame_start) {
    uint16_t joystick_x = read_adc(ADC_CHANNEL_1);
    uint16_t joystick_y = read_adc(ADC_CHANNEL_2);
    float move_x = 0.0f, move_y = 0.0f;
    if (joystick_x < 1500) {
        player.angle -= ROTATION_SPEED;
    } else if (joystick_x > 2500) {
        player.angle += ROTATION_SPEED;
    }
    if (joystick_y < 1500) {
        move_x = cosf(player.angle) * PLAYER_SPEED;
        move_y = sinf(player.angle) * PLAYER_SPEED;
    } else if (joystick_y > 2500) {
        move_x = -cosf(player.angle) * PLAYER_SPEED;
        move_y = -sinf(player.angle) * PLAYER_SPEED;
    }
    float new_x = player.x + move_x;
    float new_y = player.y + move_y;
    if (map[(int)new_y][(int)player.x] == 0) {
        player.x = new_x;
    }
    if (map[(int)player.y][(int)new_x] == 0) {
        player.y = new_y;
    }
    if (player.x < 1.0f) player.x = 1.0f;
    if (player.x > MAP_SIZE - 2.0f) player.x = MAP_SIZE - 2.0f;
    if (player.y < 1.0f) player.y = 1.0f;
    if (player.y > MAP_SIZE - 2.0f) player.y = MAP_SIZE - 2.0f;
}

static void update_enemies(uint32_t current_time) {
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (enemies[i].active) {
            // Handle flashing for death
            if (enemies[i].flash_count > 0) {
                enemies[i].flash_count--;
                if (enemies[i].flash_count == 0) {
                    enemies[i].active = 0;
                    player.kills++;
                    spawn_pickup(enemies[i].x, enemies[i].y);
                    play_sound(600, 40, 100);
                }
                continue;
            }
            
            float dx = player.x - enemies[i].x;
            float dy = player.y - enemies[i].y;
            float dist = sqrtf(dx * dx + dy * dy);
            
            
            if (dist > 0.8f) { 
                float move_x = (dx / dist) * ENEMY_SPEED;
                float move_y = (dy / dist) * ENEMY_SPEED;
                
               
                float test_x = enemies[i].x + move_x;
                float test_y = enemies[i].y + move_y;
                
                
                int grid_y = (int)enemies[i].y;
                int grid_x = (int)test_x;
                if (grid_x >= 1 && grid_x < MAP_SIZE - 1 && 
                    grid_y >= 1 && grid_y < MAP_SIZE - 1 && 
                    map[grid_y][grid_x] == 0) {
                    enemies[i].x = test_x;
                }
                
               
                grid_x = (int)enemies[i].x;
                grid_y = (int)test_y;
                if (grid_x >= 1 && grid_x < MAP_SIZE - 1 && 
                    grid_y >= 1 && grid_y < MAP_SIZE - 1 && 
                    map[grid_y][grid_x] == 0) {
                    enemies[i].y = test_y;
                }
            }
            
           
            if (dist < 2.0f && current_time - last_damage_time > DAMAGE_INTERVAL_MS) {
                int damage = is_defending ? 1 : 2;
                player.hp -= damage;
                last_damage_time = current_time;
                red_flash = 1;
                red_flash_end = current_time + 100;
                play_sound(150, 40, 100);
            }
        }
    }
}

static void melee_attack(uint32_t current_time) {
    if (current_time - last_melee_time < MELEE_COOLDOWN_MS) return;

    int hit = 0;
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (enemies[i].active && enemies[i].flash_count == 0) {
            float dx = player.x - enemies[i].x;
            float dy = player.y - enemies[i].y;
            float dist = sqrtf(dx * dx + dy * dy);
            if (dist < MELEE_RANGE) {
                enemies[i].hp = 0;
                enemies[i].flash_count = 4;
                hit = 1;
            }
        }
    }

    if (hit) {
        last_melee_time = current_time;
        melee_active = 1;
        melee_end_time = current_time + MELEE_DISPLAY_MS;
        play_sound(400, 50, 150);
    }
}

static void update_pickups(void) {
    for (int i = 0; i < 5; i++) {
        if (pickups[i].active) {
            float dx = pickups[i].x - player.x;
            float dy = pickups[i].y - player.y;
            float dist = sqrtf(dx * dx + dy * dy);
            if (dist < 1.5f) {
                player.hp += HEAL_AMOUNT;
                if (player.hp > PLAYER_HP_MAX) {
                    player.hp = PLAYER_HP_MAX;
                }
                pickups[i].active = 0;
            }
        }
    }
}

static void play_happy_melody(void) {
    buzzer_tone(&buzzer_cfg, 523, 50);
    HAL_Delay(200);
    buzzer_off(&buzzer_cfg);
    HAL_Delay(50);
    
    buzzer_tone(&buzzer_cfg, 659, 50);
    HAL_Delay(200);
    buzzer_off(&buzzer_cfg);
    HAL_Delay(50);
    
    buzzer_tone(&buzzer_cfg, 784, 50);
    HAL_Delay(200);
    buzzer_off(&buzzer_cfg);
    HAL_Delay(50);
    
    buzzer_tone(&buzzer_cfg, 1047, 50);
    HAL_Delay(300);
    buzzer_off(&buzzer_cfg);
    HAL_Delay(2100);
}

static void play_sad_melody(void) {
    buzzer_tone(&buzzer_cfg, 220, 40);
    HAL_Delay(300);
    buzzer_off(&buzzer_cfg);
    HAL_Delay(100);
    
    buzzer_tone(&buzzer_cfg, 196, 40);
    HAL_Delay(300);
    buzzer_off(&buzzer_cfg);
    HAL_Delay(100);
    
    buzzer_tone(&buzzer_cfg, 175, 40);
    HAL_Delay(300);
    buzzer_off(&buzzer_cfg);
    HAL_Delay(2100);
}

MenuState Game2_Run(void) {
    uint32_t current_time = HAL_GetTick();
    init_map();
    init_player();
    init_enemies();
    init_bullets();
    init_pickups();
    last_spawn_time = current_time;
    last_damage_time = current_time;
    red_flash = 0;
    is_defending = 0;
    buzzer_active = 0;
    crosshair_hit = 0;
    melee_active = 0;
    last_melee_time = 0;
    
    LCD_Fill_Buffer(0);
    LCD_printString("DOOOM", 40, 110, 1, 4);
    LCD_Refresh(&cfg0);
    
    // Intro melody with proper stops
    buzzer_tone(&buzzer_cfg, 392, 50);
    HAL_Delay(500);
    buzzer_off(&buzzer_cfg);
    HAL_Delay(50);
    buzzer_tone(&buzzer_cfg, 523, 50);
    HAL_Delay(500);
    buzzer_off(&buzzer_cfg);
    
    while (1) {
        current_time = HAL_GetTick();
        uint32_t frame_start = current_time;
        Input_Read();
        
        // BT3 button = return to main menu
        if (current_input.btn3_pressed) {
            buzzer_off(&buzzer_cfg);
            return MENU_STATE_HOME;
        }
        
        // B1 button = shoot
        if (current_input.btn1_pressed) {
            shoot();
        }
        
        
        is_defending = current_input.btn3_held;

        // BT2 button = melee attack (instant kill at close range)
        if (current_input.btn2_pressed) {
            melee_attack(current_time);
        }
        
        int active_count = 0;
        for (int i = 0; i < MAX_ENEMIES; i++) {
            if (enemies[i].active && enemies[i].hp > 0) active_count++;
        }
        if (active_count < MAX_ENEMIES && current_time - last_spawn_time > SPAWN_INTERVAL_MS) {
            spawn_enemy();
            last_spawn_time = current_time;
        }
        
        update_player(frame_start);
        update_enemies(current_time);
        update_bullets();
        update_pickups();
        update_buzzer();
        
        LCD_Fill_Buffer(0);
        draw_3d();
        draw_sprites();
        draw_bullets();
        draw_gun();
        draw_shield();
        draw_knife();
        draw_crosshair();
        draw_ui();
        
        if (red_flash && current_time < red_flash_end) {
            for (int y = 0; y < 280; y++) {
                for (int x = 0; x < 240; x++) {
                    uint8_t c = LCD_Get_Pixel(x, y);
                    if (c == 0) {
                        LCD_Set_Pixel(x, y, 2);
                    }
                }
            }
        } else {
            red_flash = 0;
        }
        
        LCD_Refresh(&cfg0);
        
        if (player.kills >= KILLS_TO_WIN && player.hp >= 10) {
            buzzer_off(&buzzer_cfg);
            LCD_Fill_Buffer(0);
            LCD_printString("MISSION", 30, 100, 3, 3);
            LCD_printString("COMPLETE", 20, 140, 3, 3);
            LCD_Refresh(&cfg0);
            play_happy_melody();
            return MENU_STATE_HOME;
        }
        
        if (player.kills >= KILLS_TO_WIN && player.hp < 10) {
            buzzer_off(&buzzer_cfg);
            LCD_Fill_Buffer(0);
            LCD_printString("MISSION", 30, 80, 2, 3);
            LCD_printString("INTERRUPTED", 5, 120, 2, 2);
            LCD_printString("RETREAT", 35, 160, 2, 3);
            LCD_Refresh(&cfg0);
            play_sad_melody();
            return MENU_STATE_HOME;
        }
        
        if (player.hp <= 0) {
            buzzer_off(&buzzer_cfg);
            LCD_Fill_Buffer(0);
            LCD_printString("MISSION", 30, 100, 2, 3);
            LCD_printString("FAILED", 40, 140, 2, 3);
            LCD_Refresh(&cfg0);
            play_sad_melody();
            return MENU_STATE_HOME;
        }
        
        uint32_t frame_time = HAL_GetTick() - frame_start;
        if (frame_time < GAME_FRAME_MS) {
            HAL_Delay(GAME_FRAME_MS - frame_time);
        }
    }
}
