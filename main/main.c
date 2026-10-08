#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/pulse_cnt.h"
#include "esp_adc/adc_oneshot.h" 
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "nvs_flash.h" 
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

// Pinos do hardware
#define MOTOR_ESQ_IN1 4
#define MOTOR_ESQ_IN2 5
#define MOTOR_DIR_IN1 6
#define MOTOR_DIR_IN2 7
#define ENC_ESQ_DT    11
#define ENC_ESQ_CLK   12
#define ENC_DIR_DT    14
#define ENC_DIR_CLK   13
#define TRIG_PIN      35  
#define ECHO_PIN      36
#define SOLENOIDE_PIN 10
#define PINO_BATERIA  ADC_CHANNEL_1 

// Modelagem cinemática
#define DIAMETRO_RODA_CM 6.671
#define PERIMETRO_CM     (3.14159 * DIAMETRO_RODA_CM)
#define PULSOS_POR_VOLTA 43.0 
#define CM_POR_PULSO (PERIMETRO_CM / PULSOS_POR_VOLTA)
#define TEMPO_LOOP_S     0.05 

// Tração e zona morta
#define DEADBAND_ESQ 78 
#define DEADBAND_DIR 84 
#define MAX_PWM      255

// Ganhos KP e KI
#define KP 1.5
#define KI 0.3

// Alvos de velocidade
#define ALVO_CRUZEIRO_CMS 30.0
#define ALVO_GIRO_CMS     15.0

// Ajuste do ADC
#define FATOR_CORRECAO_BATERIA 1.08

#define CLAMP(x, min, max) ((x) < (min) ? (min) : ((x) > (max) ? (max) : (x)))

// Variáveis globais
TaskHandle_t task_chute_handle = NULL;
uint16_t ble_conn_handle = BLE_HS_CONN_HANDLE_NONE; 
uint16_t char_tx_handle; 
bool radar_ativo = false; 

pcnt_unit_handle_t pcnt_esq = NULL, pcnt_dir = NULL;
adc_oneshot_unit_handle_t adc1_handle;
float alvo_esq = 0.0, alvo_dir = 0.0;
int dist_radar_cm = 0; 

// Configuração do hardware
void init_hardware() {
    ledc_timer_config_t timer_conf = {.speed_mode = LEDC_LOW_SPEED_MODE, .duty_resolution = LEDC_TIMER_8_BIT, .timer_num = LEDC_TIMER_0, .freq_hz = 1000, .clk_cfg = LEDC_AUTO_CLK};
    ledc_timer_config(&timer_conf);

    int pinos_motor[4] = {MOTOR_ESQ_IN1, MOTOR_ESQ_IN2, MOTOR_DIR_IN1, MOTOR_DIR_IN2};
    for (int i = 0; i < 4; i++) {
        ledc_channel_config_t ch_conf = {.speed_mode = LEDC_LOW_SPEED_MODE, .channel = i, .timer_sel = LEDC_TIMER_0, .intr_type = LEDC_INTR_DISABLE, .gpio_num = pinos_motor[i], .duty = 0, .hpoint = 0};
        ledc_channel_config(&ch_conf);
    }

    pcnt_unit_config_t u_conf = {.high_limit = 32767, .low_limit = -32768};
    pcnt_new_unit(&u_conf, &pcnt_esq);
    pcnt_new_unit(&u_conf, &pcnt_dir);

    pcnt_chan_config_t c_esq = {.edge_gpio_num = ENC_ESQ_DT, .level_gpio_num = ENC_ESQ_CLK};
    pcnt_chan_config_t c_dir = {.edge_gpio_num = ENC_DIR_DT, .level_gpio_num = ENC_DIR_CLK};
    pcnt_channel_handle_t ch_esq, ch_dir;
    
    pcnt_new_channel(pcnt_esq, &c_esq, &ch_esq);
    pcnt_new_channel(pcnt_dir, &c_dir, &ch_dir);

    pcnt_channel_set_edge_action(ch_esq, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE);
    pcnt_channel_set_level_action(ch_esq, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
    pcnt_channel_set_edge_action(ch_dir, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE);
    pcnt_channel_set_level_action(ch_dir, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);

    pcnt_unit_enable(pcnt_esq); pcnt_unit_clear_count(pcnt_esq); pcnt_unit_start(pcnt_esq);
    pcnt_unit_enable(pcnt_dir); pcnt_unit_clear_count(pcnt_dir); pcnt_unit_start(pcnt_dir);

    gpio_set_direction(SOLENOIDE_PIN, GPIO_MODE_OUTPUT); gpio_set_level(SOLENOIDE_PIN, 0);
    gpio_set_direction(TRIG_PIN, GPIO_MODE_OUTPUT);      gpio_set_level(TRIG_PIN, 0);
    gpio_set_direction(ECHO_PIN, GPIO_MODE_INPUT);

    adc_oneshot_unit_init_cfg_t init_config1 = { .unit_id = ADC_UNIT_1 };
    adc_oneshot_new_unit(&init_config1, &adc1_handle);
    adc_oneshot_chan_cfg_t config_adc = { .bitwidth = ADC_BITWIDTH_DEFAULT, .atten = ADC_ATTEN_DB_12 };
    adc_oneshot_config_channel(adc1_handle, PINO_BATERIA, &config_adc);
}

// Acionamento da ponte H
void set_pwm_motores(int pwm_esq, int pwm_dir) {
    pwm_esq = CLAMP(pwm_esq, -MAX_PWM, MAX_PWM);
    pwm_dir = CLAMP(pwm_dir, -MAX_PWM, MAX_PWM);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, pwm_esq >= 0 ? pwm_esq : 0);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, pwm_esq < 0 ? -pwm_esq : 0);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2, pwm_dir >= 0 ? pwm_dir : 0);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_3, pwm_dir < 0 ? -pwm_dir : 0);
    for(int i = 0; i < 4; i++) ledc_update_duty(LEDC_LOW_SPEED_MODE, i);
}

// Solenoide
void task_chute(void *pvP) {
    while(1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY); 
        gpio_set_level(SOLENOIDE_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(60)); 
        gpio_set_level(SOLENOIDE_PIN, 0);
    }
}

// Sensor de obstáculos
void task_ultrassonico(void *pvP) {
    while(1) {
        if(ble_conn_handle != BLE_HS_CONN_HANDLE_NONE && radar_ativo) {
            gpio_set_level(TRIG_PIN, 1);
            esp_rom_delay_us(10);
            gpio_set_level(TRIG_PIN, 0);

            int64_t t = esp_timer_get_time();
            while(!gpio_get_level(ECHO_PIN) && (esp_timer_get_time() - t) < 5000);
            t = esp_timer_get_time();
            while(gpio_get_level(ECHO_PIN) && (esp_timer_get_time() - t) < 25000);
            
            int dist = (esp_timer_get_time() - t) / 58;
            if (dist > 0 && dist < 400) dist_radar_cm = dist;
        }
        vTaskDelay(pdMS_TO_TICKS(100)); 
    }
}

// Malha fechada
void task_controle_pi(void *pvP) {
    float int_esq = 0, int_dir = 0;
    int cont_esq = 0, cont_dir = 0;
    char tx_buffer[30]; 

    float alvo_atual_esq = 0.0;
    float alvo_atual_dir = 0.0;
    const float PASSO_RAMPA = 10.0; 

    float v_bat_filtrada = 8.4; 
    bool primeira_leitura = true;
    int bat_pct = 100;
    int loop_cont = 0;

    while(1) {
        // Controle nível bateria
        if (loop_cont % 20 == 0) {
            int adc_raw;
            adc_oneshot_read(adc1_handle, PINO_BATERIA, &adc_raw);
            
            float v_pino = (adc_raw / 4095.0) * 3.3; 
            float v_bat_instantanea = v_pino * ((21.0 + 9.9) / 9.9) * FATOR_CORRECAO_BATERIA;

            if (primeira_leitura) {
                if (v_bat_instantanea > 5.0) { 
                    v_bat_filtrada = v_bat_instantanea;
                    primeira_leitura = false;
                }
            } else {
                if (alvo_atual_esq == 0.0 && alvo_atual_dir == 0.0) {
                    v_bat_filtrada = (v_bat_filtrada * 0.90) + (v_bat_instantanea * 0.10);
                }
            }

            bat_pct = (int)(((v_bat_filtrada - 6.6) / (8.4 - 6.6)) * 100.0);
            bat_pct = CLAMP(bat_pct, 0, 100);
        }
        loop_cont++;

        // Leitura dos encoders
        pcnt_unit_get_count(pcnt_esq, &cont_esq); pcnt_unit_clear_count(pcnt_esq);
        pcnt_unit_get_count(pcnt_dir, &cont_dir); pcnt_unit_clear_count(pcnt_dir);

        float v_esq = (cont_esq * CM_POR_PULSO) / TEMPO_LOOP_S;
        float v_dir = (cont_dir * CM_POR_PULSO) / TEMPO_LOOP_S;
        
        // Rampa de aceleração
        if (alvo_atual_esq < alvo_esq) {
            alvo_atual_esq += PASSO_RAMPA;
            if (alvo_atual_esq > alvo_esq) alvo_atual_esq = alvo_esq;
        } else if (alvo_atual_esq > alvo_esq) {
            alvo_atual_esq -= PASSO_RAMPA;
            if (alvo_atual_esq < alvo_esq) alvo_atual_esq = alvo_esq;
        }

        if (alvo_atual_dir < alvo_dir) {
            alvo_atual_dir += PASSO_RAMPA;
            if (alvo_atual_dir > alvo_dir) alvo_atual_dir = alvo_dir;
        } else if (alvo_atual_dir > alvo_dir) {
            alvo_atual_dir -= PASSO_RAMPA;
            if (alvo_atual_dir < alvo_dir) alvo_atual_dir = alvo_dir;
        }
        
        // Controlador PI
        if (alvo_atual_esq == 0.0 && alvo_atual_dir == 0.0) {
            int_esq = int_dir = 0;
            set_pwm_motores(0, 0);
        } else {
            float err_esq = alvo_atual_esq - v_esq;
            float err_dir = alvo_atual_dir - v_dir;

            int_esq = CLAMP(int_esq + err_esq, -300, 300);
            int_dir = CLAMP(int_dir + err_dir, -300, 300);

            int pid_esq = (int)((KP * err_esq) + (KI * int_esq));
            int pid_dir = (int)((KP * err_dir) + (KI * int_dir));

            int pwm_esq = 0, pwm_dir = 0;
            if (alvo_atual_esq > 0) pwm_esq = DEADBAND_ESQ + pid_esq;
            else if (alvo_atual_esq < 0) pwm_esq = -DEADBAND_ESQ + pid_esq;

            if (alvo_atual_dir > 0) pwm_dir = DEADBAND_DIR + pid_dir;
            else if (alvo_atual_dir < 0) pwm_dir = -DEADBAND_DIR + pid_dir;

            set_pwm_motores(pwm_esq, pwm_dir);
        }

        // Telemetria via Bluetooth
        if(ble_conn_handle != BLE_HS_CONN_HANDLE_NONE && radar_ativo) {
            static float vel_media_filtrada = 0.0;
            float vel_media_instantanea = (v_esq + v_dir) / 2.0; 
            
            // Filtro Passa-Baixa encoders para estabilizar a velocidade média
            vel_media_filtrada = (vel_media_filtrada * 0.85) + (vel_media_instantanea * 0.15);

            // Zera completamente o painel se os motores estiverem parados
            if (alvo_atual_esq == 0.0 && alvo_atual_dir == 0.0) vel_media_filtrada = 0.0;

            snprintf(tx_buffer, sizeof(tx_buffer), "%d;%.2f;%d", dist_radar_cm, vel_media_filtrada, bat_pct);
            struct os_mbuf *tx_om = ble_hs_mbuf_from_flat(tx_buffer, strlen(tx_buffer));
            if (tx_om && ble_gatts_notify_custom(ble_conn_handle, char_tx_handle, tx_om) != 0) {
                os_mbuf_free_chain(tx_om); 
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50)); 
    }
}

// Configuração do Bluetooth (NimBLE)
static const ble_uuid16_t svc_uuid = BLE_UUID16_INIT(0x1234);
static const ble_uuid16_t rx_uuid  = BLE_UUID16_INIT(0xABCD);
static const ble_uuid16_t tx_uuid  = BLE_UUID16_INIT(0xDCBA);

static int ble_rx_cb(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctx, void *arg) {
    if (ctx->op == BLE_GATT_ACCESS_OP_WRITE_CHR && ctx->om && ctx->om->om_len > 0) {
        switch(ctx->om->om_data[0]) {
            case 'W': alvo_esq = ALVO_CRUZEIRO_CMS;  alvo_dir = ALVO_CRUZEIRO_CMS; break;
            case 'S': alvo_esq = -ALVO_CRUZEIRO_CMS; alvo_dir = -ALVO_CRUZEIRO_CMS; break;
            case 'A': alvo_esq = -ALVO_GIRO_CMS;     alvo_dir = ALVO_GIRO_CMS; break;
            case 'D': alvo_esq = ALVO_GIRO_CMS;      alvo_dir = -ALVO_GIRO_CMS; break;
            case 'Q': alvo_esq = ALVO_CRUZEIRO_CMS / 3.0; alvo_dir = ALVO_CRUZEIRO_CMS; break;
            case 'E': alvo_esq = ALVO_CRUZEIRO_CMS; alvo_dir = ALVO_CRUZEIRO_CMS / 3.0; break;
            case 'Z': alvo_esq = -ALVO_CRUZEIRO_CMS / 3.0; alvo_dir = -ALVO_CRUZEIRO_CMS; break;
            case 'C': alvo_esq = -ALVO_CRUZEIRO_CMS; alvo_dir = -ALVO_CRUZEIRO_CMS / 3.0; break;
            case 'X': alvo_esq = 0.0;               alvo_dir = 0.0; break; 
            case 'R': xTaskNotifyGive(task_chute_handle); break; 
        }
    }
    return 0;
}

static const struct ble_gatt_svc_def gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            { .uuid = &rx_uuid.u, .access_cb = ble_rx_cb, .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP },
            { .uuid = &tx_uuid.u, .access_cb = ble_rx_cb, .flags = BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_READ, .val_handle = &char_tx_handle },
            {0,}
        }
    }, {0,}
};

static int ble_gap_event(struct ble_gap_event *event, void *arg) {
    uint8_t addr_type;
    struct ble_gap_adv_params adv_p = {.conn_mode = BLE_GAP_CONN_MODE_UND, .disc_mode = BLE_GAP_DISC_MODE_GEN};
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) ble_conn_handle = event->connect.conn_handle;
            else { ble_hs_id_infer_auto(0, &addr_type); ble_gap_adv_start(addr_type, NULL, BLE_HS_FOREVER, &adv_p, ble_gap_event, NULL); }
            break;
        case BLE_GAP_EVENT_DISCONNECT:
            ble_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            alvo_esq = 0.0; alvo_dir = 0.0; radar_ativo = false;
            ble_hs_id_infer_auto(0, &addr_type); ble_gap_adv_start(addr_type, NULL, BLE_HS_FOREVER, &adv_p, ble_gap_event, NULL);
            break;
        case BLE_GAP_EVENT_SUBSCRIBE:
            if (event->subscribe.attr_handle == char_tx_handle) radar_ativo = event->subscribe.cur_notify;
            break;
    }
    return 0;
}

static void ble_on_sync(void) {
    uint8_t addr_type; ble_hs_id_infer_auto(0, &addr_type);
    struct ble_hs_adv_fields f = {.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP, .name = (uint8_t *)"Bot-afogo", .name_len = 9, .name_is_complete = 1};
    ble_gap_adv_set_fields(&f);
    struct ble_gap_adv_params adv_p = {.conn_mode = BLE_GAP_CONN_MODE_UND, .disc_mode = BLE_GAP_DISC_MODE_GEN};
    ble_gap_adv_start(addr_type, NULL, BLE_HS_FOREVER, &adv_p, ble_gap_event, NULL);
}

void ble_host_task(void *param) { nimble_port_run(); nimble_port_freertos_deinit(); }

void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase()); nvs_flash_init();
    }
    init_hardware();

    xTaskCreatePinnedToCore(task_controle_pi, "Task_PI", 4096, NULL, 6, NULL, 1); 
    xTaskCreatePinnedToCore(task_chute, "Task_Chute", 2048, NULL, 5, &task_chute_handle, 1);
    xTaskCreatePinnedToCore(task_ultrassonico, "Task_Sonar", 4096, NULL, 4, NULL, 1); 

    nimble_port_init();
    ble_svc_gap_device_name_set("Bot-afogo");
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_gatts_count_cfg(gatt_svcs);
    ble_gatts_add_svcs(gatt_svcs);

    ble_hs_cfg.sync_cb = ble_on_sync;
    nimble_port_freertos_init(ble_host_task);
}