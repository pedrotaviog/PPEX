# 🤖 PPEX (GRUPO 1) - Robô de Futebol Autônomo (ESP32-S3)

[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.5-red.svg)](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/)
[![C](https://img.shields.io/badge/Language-C-blue.svg)](https://en.wikipedia.org/wiki/C_(programming_language))
[![FreeRTOS](https://img.shields.io/badge/RTOS-FreeRTOS-green.svg)](https://www.freertos.org/)

Repositório dedicado ao firmware e documentação do robô móvel autônomo desenvolvido para a disciplina de **Planejamento e Prática de Experimentos (PPEX)** do CEFET-MG. 

## 🚀 Principais Funcionalidades

*   **Navegação Autónoma:** Leitura de distância em tempo real utilizando o sensor ultrassónico HC-SR04 para desvio de obstáculos.
*   **Sistema de Remate (Chute):** Acionamento de uma solenoide através de um pulso ultrarrápido (60ms). A energia é fornecida por um banco de condensadores (4x 2200µF) carregado via módulo Step-Up (CN6009), isolado logicamente por um optoacoplador (PC817) e comutado por um MOSFET de potência (IRF530).
*   **Gestão de Energia Independente:** Circuitos de potência e lógica isolados, garantindo a integridade do microcontrolador durante picos de corrente e ruído eletromagnético.

## 🛠️ Especificações de Hardware

A PCB do projeto (disponível no esquemático) integra os seguintes componentes:
*   **Microcontrolador:** ESP32-S3 (DevKit)
*   **Tração:** 2x Motores DC com caixa de redução
*   **Driver de Motores:** TC1508A (Ponte H dupla) controlada via sinais PWM (LEDC)
*   **Sensores de Odometria:** 2x Encoders magnéticos/ópticos (Canais A e B)
*   **Sensor de Ambiente:** HC-SR04 (Ultrassónico)
*   **Conversão DC-DC:** MT3608 (lógica) e HW-045 / CN6009 (potência do chute)

## 💻 Estrutura de Software

O código foi desenvolvido de forma nativa e enxuta utilizando o *framework* oficial da Espressif, abandonando a camada do Arduino para extrair o máximo de desempenho do silício.

*   `driver/gpio.h`: Controlo digital da ponte H e acionamento da solenoide.
*   `driver/ledc.h`: Geração de PWM por hardware para controlo preciso da velocidade de cruzeiro dos motores.
*   `driver/pulse_cnt.h`: Leitura assíncrona dos encoders de quadratura com filtros de *glitch* por hardware, não sobrecarregando a CPU com interrupções.
*   `freertos/task.h`: Gestão de *delays* (*vTaskDelay*) e concorrência.

## ⚙️ Como Compilar e Executar

1. Instale o [ESP-IDF v5.5](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/index.html).
2. Clone este repositório.
3. Certifique-se de que o ambiente do ESP-IDF está ativado no seu terminal (ou utilize a extensão oficial no VSCode).
4. Execute o *Build* e o *Flash* para o ESP32-S3 através da porta UART.

```bash
idf.py build
idf.py -p COMx flash monitor
```
---

### Autores:

- Ana Liz Rodrigues Ferreira 
- Arthur Rocha Miranda
- ⁠Jeferson Rodrigues de Souza
- ⁠João Pedro Coelho Costa
- ⁠Kaynã Teixeira Braga do Prado
- ⁠Mohamad Lotfi Natour 
- Pedro Otávio Gaspar de Freitas
- ⁠Pedro Rabelo
- ⁠Samuel Carvalho Assunção Horta
