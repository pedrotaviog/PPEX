#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_timer.h"
#include "rom/ets_sys.h" 

// --- Pinos dos Motores ---
#define MOTOR_ESQ_IN1 GPIO_NUM_4
#define MOTOR_ESQ_IN2 GPIO_NUM_5
#define MOTOR_DIR_IN1 GPIO_NUM_6
#define MOTOR_DIR_IN2 GPIO_NUM_7

// --- Pinos do Ultrassônico e Solenoide ---
#define TRIG_PIN GPIO_NUM_35
#define ECHO_PIN GPIO_NUM_36
#define SOLENOIDE_PIN GPIO_NUM_10

// --- Configurações de PWM ---
#define LEDC_MODE               LEDC_LOW_SPEED_MODE
#define LEDC_TIMER              LEDC_TIMER_0
#define LEDC_DUTY_RES           LEDC_TIMER_8_BIT 
#define LEDC_FREQUENCY          1000             

// --- Parâmetros Fixos ---
#define PWM_ESQ_CRUZEIRO        130 
#define PWM_DIR_CRUZEIRO        130 
#define VELOCIDADE_GIRO         150
#define DISTANCIA_PARADA_CM     30.0  

void init_pwm_motores() {
    ledc_timer_config_t timer_conf = {
        .speed_mode = LEDC_MODE, .duty_resolution = LEDC_DUTY_RES,
        .timer_num = LEDC_TIMER, .freq_hz = LEDC_FREQUENCY, .clk_cfg = LEDC_AUTO_CLK
    };
    ledc_timer_config(&timer_conf);

    int pinos_motor[4] = {MOTOR_ESQ_IN1, MOTOR_ESQ_IN2, MOTOR_DIR_IN1, MOTOR_DIR_IN2};
    for (int i = 0; i < 4; i++) {
        ledc_channel_config_t channel_conf = {
            .speed_mode = LEDC_MODE, .channel = i, .timer_sel = LEDC_TIMER,
            .intr_type = LEDC_INTR_DISABLE, .gpio_num = pinos_motor[i], .duty = 0, .hpoint = 0
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
    int32_t timeout_us = 20000; // 20ms para não travar o loop

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

    vTaskDelay(2000 / portTICK_PERIOD_MS);

    while (1) {
        float distancia = ler_distancia();
        
        if (distancia < DISTANCIA_PARADA_CM) {
            // Freia 
            set_motores(0, 0);
            vTaskDelay(500 / portTICK_PERIOD_MS);
            
            // Gira no próprio eixo
            set_motores(-VELOCIDADE_GIRO, VELOCIDADE_GIRO);
            vTaskDelay(600 / portTICK_PERIOD_MS); 
            
            set_motores(0, 0);
            vTaskDelay(500 / portTICK_PERIOD_MS); 

            printf("CHUTE!\n");
            gpio_set_level(SOLENOIDE_PIN, 1);
            vTaskDelay(60 / portTICK_PERIOD_MS); 
            gpio_set_level(SOLENOIDE_PIN, 0);

            vTaskDelay(1000 / portTICK_PERIOD_MS); 
            
        } else {
            set_motores(PWM_ESQ_CRUZEIRO, PWM_DIR_CRUZEIRO);
        }

        vTaskDelay(30 / portTICK_PERIOD_MS);
    }
}