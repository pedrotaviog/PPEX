#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/pulse_cnt.h" 
#include "esp_timer.h"
#include "rom/ets_sys.h" 

// --- Pinos dos Motores ---
#define MOTOR_IN4 GPIO_NUM_7
#define MOTOR_IN3 GPIO_NUM_6
#define MOTOR_IN2 GPIO_NUM_5
#define MOTOR_IN1 GPIO_NUM_4

// --- Pinos do Ultrassônico ---
#define TRIG_PIN GPIO_NUM_35
#define ECHO_PIN GPIO_NUM_36

// --- Pino da Solenoide ---
#define SOLENOIDE_PIN GPIO_NUM_10

// --- Pinos dos Encoders ---
#define ENC_ESQ_A GPIO_NUM_14
#define ENC_ESQ_B GPIO_NUM_13
#define ENC_DIR_B GPIO_NUM_12
#define ENC_DIR_A GPIO_NUM_11

// --- Configurações de PWM ---
#define LEDC_MODE               LEDC_LOW_SPEED_MODE
#define LEDC_TIMER              LEDC_TIMER_0
#define LEDC_DUTY_RES           LEDC_TIMER_8_BIT 
#define LEDC_FREQUENCY          1000             

// Velocidade reduzida de teste (0 a 255)
#define VELOCIDADE_CRUZEIRO     130 
#define VELOCIDADE_GIRO         150
#define DISTANCIA_PARADA_CM     20.0  

// Variáveis globais para os contadores
pcnt_unit_handle_t pcnt_esq = NULL;
pcnt_unit_handle_t pcnt_dir = NULL;

// Função para configurar a decodificação de quadratura dos encoders
pcnt_unit_handle_t setup_encoder(int pin_a, int pin_b) {
    pcnt_unit_config_t unit_config = {
        .high_limit = 32767,
        .low_limit = -32768,
    };
    pcnt_unit_handle_t pcnt_unit = NULL;
    pcnt_new_unit(&unit_config, &pcnt_unit);

    // Filtro para ignorar ruídos elétricos do motor
    pcnt_glitch_filter_config_t filter_config = {
        .max_glitch_ns = 1000,
    };
    pcnt_unit_set_glitch_filter(pcnt_unit, &filter_config);

    pcnt_chan_config_t chan_a_config = {.edge_gpio_num = pin_a, .level_gpio_num = pin_b};
    pcnt_channel_handle_t chan_a = NULL;
    pcnt_new_channel(pcnt_unit, &chan_a_config, &chan_a);

    pcnt_chan_config_t chan_b_config = {.edge_gpio_num = pin_b, .level_gpio_num = pin_a};
    pcnt_channel_handle_t chan_b = NULL;
    pcnt_new_channel(pcnt_unit, &chan_b_config, &chan_b);

    pcnt_channel_set_edge_action(chan_a, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE);
    pcnt_channel_set_level_action(chan_a, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
    pcnt_channel_set_edge_action(chan_b, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE);
    pcnt_channel_set_level_action(chan_b, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);

    pcnt_unit_enable(pcnt_unit);
    pcnt_unit_clear_count(pcnt_unit);
    pcnt_unit_start(pcnt_unit);
    
    return pcnt_unit;
}

void init_pwm_motores() {
    ledc_timer_config_t timer_conf = {
        .speed_mode       = LEDC_MODE,
        .duty_resolution  = LEDC_DUTY_RES,
        .timer_num        = LEDC_TIMER,
        .freq_hz          = LEDC_FREQUENCY,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ledc_timer_config(&timer_conf);

    int pinos_motor[4] = {MOTOR_IN1, MOTOR_IN2, MOTOR_IN3, MOTOR_IN4};
    for (int i = 0; i < 4; i++) {
        ledc_channel_config_t channel_conf = {
            .speed_mode     = LEDC_MODE,
            .channel        = i, 
            .timer_sel      = LEDC_TIMER,
            .intr_type      = LEDC_INTR_DISABLE,
            .gpio_num       = pinos_motor[i],
            .duty           = 0,
            .hpoint         = 0
        };
        ledc_channel_config(&channel_conf);
    }
}

void set_motores(int vel_esq, int vel_dir) {
    if (vel_esq >= 0) {
        ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_0, vel_esq);
        ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_1, 0);
    } else {
        ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_0, 0);
        ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_1, -vel_esq);
    }
    
    if (vel_dir >= 0) {
        ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_2, vel_dir);
        ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_3, 0);
    } else {
        ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_2, 0);
        ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_3, -vel_dir);
    }

    for(int i = 0; i < 4; i++) ledc_update_duty(LEDC_MODE, i);
}

void init_ultrassonico() {
    gpio_set_direction(TRIG_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(ECHO_PIN, GPIO_MODE_INPUT);
    gpio_set_level(TRIG_PIN, 0);
}

void init_solenoide() {
    gpio_set_direction(SOLENOIDE_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(SOLENOIDE_PIN, 0); 
}

float ler_distancia() {
    gpio_set_level(TRIG_PIN, 1);
    ets_delay_us(10);
    gpio_set_level(TRIG_PIN, 0);

    int64_t start_time = esp_timer_get_time();
    int32_t timeout_us = 30000; 

    while (gpio_get_level(ECHO_PIN) == 0 && (esp_timer_get_time() - start_time) < timeout_us) {}
    int64_t echo_start = esp_timer_get_time();

    while (gpio_get_level(ECHO_PIN) == 1 && (esp_timer_get_time() - echo_start) < timeout_us) {}
    int64_t echo_end = esp_timer_get_time();

    int64_t echo_duration = echo_end - echo_start;
    if (echo_duration >= timeout_us - 1000) return 999.0; 
    return echo_duration * 0.01715; 
}

void app_main(void) {
    init_pwm_motores();
    init_ultrassonico();
    init_solenoide();

    // Inicializa os encoders
    pcnt_esq = setup_encoder(ENC_ESQ_A, ENC_ESQ_B);
    pcnt_dir = setup_encoder(ENC_DIR_A, ENC_DIR_B);

    vTaskDelay(2000 / portTICK_PERIOD_MS);

    while (1) {
        int pulsos_esq = 0;
        int pulsos_dir = 0;
        
        // Lê os valores dos encoders
        pcnt_unit_get_count(pcnt_esq, &pulsos_esq);
        pcnt_unit_get_count(pcnt_dir, &pulsos_dir);

        float distancia = ler_distancia();
        
        // Imprime o diagnóstico completo no terminal
        printf("Dist: %.1f cm | Enc Esq: %d | Enc Dir: %d\n", distancia, pulsos_esq, pulsos_dir);

        if (distancia < DISTANCIA_PARADA_CM) {
            printf("Obstaculo detectado! Parando e girando...\n");
            
            set_motores(0, 0);
            vTaskDelay(500 / portTICK_PERIOD_MS);
            
            set_motores(-VELOCIDADE_GIRO, VELOCIDADE_GIRO);
            vTaskDelay(800 / portTICK_PERIOD_MS); 
            
            set_motores(0, 0);
            vTaskDelay(1000 / portTICK_PERIOD_MS); 

            printf("CHUTE!\n");
            gpio_set_level(SOLENOIDE_PIN, 1);
            vTaskDelay(60 / portTICK_PERIOD_MS); 
            gpio_set_level(SOLENOIDE_PIN, 0);

            vTaskDelay(1000 / portTICK_PERIOD_MS); 
            
            // Zera os encoders após a manobra para não acumular erros
            pcnt_unit_clear_count(pcnt_esq);
            pcnt_unit_clear_count(pcnt_dir);
            
        } else {
            set_motores(VELOCIDADE_CRUZEIRO, VELOCIDADE_CRUZEIRO);
        }

        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
}