#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_log.h"

static const char *TAG = "DODGER";

#define GRID_ROWS 5
#define GRID_COLS 5

static const int ROW_PINS[GRID_ROWS]   = {13, 27, 33,  4, 15};
static const int COL_R_PINS[GRID_COLS] = {12, 26, 32, 18,  0}; // LEDs rojos
static const int COL_G_PINS[GRID_COLS] = {14, 25, 23,  5,  2}; // LEDs verdes

#define BTN_LEFT  19
#define BTN_RIGHT 22

#define DISPLAY_PERIOD_US 3500  

static uint8_t g_fb[GRID_ROWS][GRID_COLS];

static void fb_clear(void) { memset(g_fb, 0, sizeof(g_fb)); }
static void fb_fill(uint8_t c) { memset(g_fb, c, sizeof(g_fb)); }
static void fb_set(int r, int c, uint8_t color) {
    if (r >= 0 && r < GRID_ROWS && c >= 0 && c < GRID_COLS)
        g_fb[r][c] = color;
}

static volatile int disp_row = 0;

static void IRAM_ATTR display_isr(void *arg)
{
    // Apagar la fila anterior
    int prev = (disp_row + GRID_ROWS - 1) % GRID_ROWS;
    gpio_set_level(ROW_PINS[prev], 0);

    // Apagar todas las columnas
    for (int c = 0; c < GRID_COLS; c++) {
        gpio_set_level(COL_G_PINS[c], 0);
        gpio_set_level(COL_R_PINS[c], 0);
    }

    // Encender columnas según el buffer de la fila actual
    int r = disp_row;
    for (int c = 0; c < GRID_COLS; c++) {
        uint8_t pix = g_fb[r][c];
        if (pix == 1) gpio_set_level(COL_G_PINS[c], 1); // verde
        if (pix == 2) gpio_set_level(COL_R_PINS[c], 1); // rojo
    }

    // Activar fila actual
    gpio_set_level(ROW_PINS[r], 1);
    disp_row = (disp_row + 1) % GRID_ROWS;
}

static void gpio_init_all(void)
{
    for (int i = 0; i < GRID_ROWS; i++) {
        gpio_reset_pin(ROW_PINS[i]);
        gpio_set_direction(ROW_PINS[i], GPIO_MODE_OUTPUT);
        gpio_set_level(ROW_PINS[i], 0);
        gpio_set_drive_capability(ROW_PINS[i], GPIO_DRIVE_CAP_3);
    }
    for (int i = 0; i < GRID_COLS; i++) {
        gpio_reset_pin(COL_G_PINS[i]);
        gpio_set_direction(COL_G_PINS[i], GPIO_MODE_OUTPUT);
        gpio_set_level(COL_G_PINS[i], 0);
        gpio_set_drive_capability(COL_G_PINS[i], GPIO_DRIVE_CAP_3);

        gpio_reset_pin(COL_R_PINS[i]);
        gpio_set_direction(COL_R_PINS[i], GPIO_MODE_OUTPUT);
        gpio_set_level(COL_R_PINS[i], 0);
        gpio_set_drive_capability(COL_R_PINS[i], GPIO_DRIVE_CAP_3);
    }


    gpio_set_pull_mode(15, GPIO_FLOATING);
    gpio_set_pull_mode(0,  GPIO_FLOATING);
    gpio_set_pull_mode(2,  GPIO_PULLDOWN_ONLY);
    gpio_set_pull_mode(12, GPIO_FLOATING);

   
    gpio_reset_pin(BTN_LEFT);
    gpio_set_direction(BTN_LEFT, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BTN_LEFT, GPIO_PULLUP_ONLY);

    gpio_reset_pin(BTN_RIGHT);
    gpio_set_direction(BTN_RIGHT, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BTN_RIGHT, GPIO_PULLUP_ONLY);
}

#define DEBOUNCE_MS 50

typedef struct {
    int pin;
    bool last_stable, current;
    TickType_t last_change;
} Button;

static Button btn_left  = {BTN_LEFT,  true, true, 0};
static Button btn_right = {BTN_RIGHT, true, true, 0};

static bool btn_pressed(Button *b)
{
    bool raw = gpio_get_level(b->pin);
    TickType_t now = xTaskGetTickCount();
    if (raw != b->current) {
        b->current = raw;
        b->last_change = now;
    }
    if ((now - b->last_change) >= pdMS_TO_TICKS(DEBOUNCE_MS)) {
        if (b->last_stable != b->current) {
            b->last_stable = b->current;
            if (!b->last_stable) return true; // flanco de bajada = presionado
        }
    }
    return false;
}


#define PLAYER_ROW      4    
#define INITIAL_SPEED   500  // ms entre cada caída del obstáculo
#define MIN_SPEED       100  // velocidad máxima 
#define SPEED_STEP       40  // cuánto se reduce cada 3 esquivados

static int jugador_col  = 2;
static int obs_fila     = 0;
static int obs_col      = 0;
static int puntaje      = 0;
static int velocidad_ms = INITIAL_SPEED;


static void dibujar(void)
{
    fb_clear();
    fb_set(PLAYER_ROW, jugador_col, 1);  // jugador en verde
    fb_set(obs_fila,   obs_col,     2);  // obstáculo en rojo
}

static void nuevo_obstaculo(void)
{
    obs_fila = 0;
    obs_col  = esp_random() % GRID_COLS;
}

static void animacion_game_over(void)
{
    for (int i = 0; i < 6; i++) {
        fb_fill(2);                          
        vTaskDelay(pdMS_TO_TICKS(200));
        fb_clear();
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}


static void animacion_esquive(void)
{
    fb_fill(1);                              
    vTaskDelay(pdMS_TO_TICKS(100));
    fb_clear();
    vTaskDelay(pdMS_TO_TICKS(100));
}


static void game_task(void *pvParam)
{
    TickType_t ultimo_caida = xTaskGetTickCount();

reiniciar:
    jugador_col  = 2;
    puntaje      = 0;
    velocidad_ms = INITIAL_SPEED;
    nuevo_obstaculo();
    ultimo_caida = xTaskGetTickCount();

    ESP_LOGI(TAG, "¡Juego iniciado!");

    while (true) {
        TickType_t ahora = xTaskGetTickCount();

       
        if (btn_pressed(&btn_left) && jugador_col > 0)
            jugador_col--;
        if (btn_pressed(&btn_right) && jugador_col < GRID_COLS - 1)
            jugador_col++;

        
        if ((ahora - ultimo_caida) >= pdMS_TO_TICKS(velocidad_ms)) {
            ultimo_caida = ahora;
            obs_fila++;

           
            if (obs_fila == PLAYER_ROW && obs_col == jugador_col) {
                ESP_LOGI(TAG, "GAME OVER — Puntaje: %d", puntaje);
                dibujar(); 
                vTaskDelay(pdMS_TO_TICKS(300));
                animacion_game_over();
                goto reiniciar;
            }

            
            if (obs_fila > PLAYER_ROW) {
                puntaje++;
                ESP_LOGI(TAG, "Esquivado! Puntaje: %d | Velocidad: %dms", puntaje, velocidad_ms);
                animacion_esquive();

                // Aumentar velocidad cada 3 esquivados
                if (puntaje % 3 == 0 && velocidad_ms > MIN_SPEED) {
                    velocidad_ms -= SPEED_STEP;
                    if (velocidad_ms < MIN_SPEED) velocidad_ms = MIN_SPEED;
                    ESP_LOGI(TAG, "Velocidad aumentada → %dms", velocidad_ms);
                }

                nuevo_obstaculo();
            }
        }

        
        dibujar();
        vTaskDelay(pdMS_TO_TICKS(16)); // ~60 fps lógico
    }
}

static void display_task(void *pvParam)
{
    esp_timer_handle_t timer;
    esp_timer_create_args_t args = {
        .callback = display_isr,
        .arg      = NULL,
        .name     = "disp"
    };
    esp_timer_create(&args, &timer);
    esp_timer_start_periodic(timer, DISPLAY_PERIOD_US);
    while (1) vTaskDelay(pdMS_TO_TICKS(1000)); 
}

void app_main(void)
{
    gpio_init_all();
    fb_clear();

    ESP_LOGI(TAG, "=== DODGER ===");

  
    xTaskCreatePinnedToCore(display_task, "display", 2048, NULL, 10, NULL, 1);
   
    xTaskCreatePinnedToCore(game_task,    "game",    4096, NULL,  5, NULL, 0);
}