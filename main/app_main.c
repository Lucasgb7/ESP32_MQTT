#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include <esp_log.h>
#include "protocol_examples_common.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "mqtt_client.h"
#include <cJSON.h>

#define BLINK_GPIO GPIO_NUM_2

static const char *TAG = "MOSQUITTO";
static uint8_t s_led_state = 0;
static const TickType_t oneSecond = pdMS_TO_TICKS(1000);
static esp_adc_cal_characteristics_t adc1_chars;

void flash(int duration)
{
    gpio_set_level(BLINK_GPIO, 1);
    vTaskDelay(duration / portTICK_PERIOD_MS);
    gpio_set_level(BLINK_GPIO, 0);
    vTaskDelay(duration / portTICK_PERIOD_MS);    
}

void sos_flash()
{
    flash(200); flash(200); flash(200); // S
    vTaskDelay(300 / portTICK_PERIOD_MS);
    flash(500); flash(500); flash(500); // O
    flash(200); flash(200); flash(200); // S
    vTaskDelay(1000 / portTICK_PERIOD_MS);
}

// Funcao para gerenciar os eventos MQTT 
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;

    switch ((esp_mqtt_event_id_t)event_id) {
case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");

        char * data = NULL;
        // Adquirindo tensao pela GPIO36 (ADC1 Canal 0)
        uint32_t voltage_raw;
        voltage_raw = esp_adc_cal_raw_to_voltage(adc1_get_raw(ADC1_CHANNEL_0), &adc1_chars);
        ESP_LOGI("ADC_READING", "ADC1_CHANNEL_0: %d mV", voltage_raw);
        // Convertendo uint32 p/ char * (10 caracteres para o valor maximo de um uint32)
        char voltage_str[10] = ""; 
        sprintf(voltage_str, "%ld", (long)voltage_raw);

        // Criando um objeto JSON para enviar mensagem MQTT
        cJSON * packet  = cJSON_CreateObject();
        cJSON * voltage_json;
        voltage_json = cJSON_CreateString((const char *) voltage_str);
        cJSON_AddItemToObject(packet, "Voltage (mV)", voltage_json);

        // Lendo uma mensagem pelo Serial Monitor
        char message[255];
        int count = 0;
        printf("Digite uma mensagem: \n");
        while (count < 255) {
            int c = fgetc(stdin);
            if (c == '\n') {
                message[count] = '\0';
                break;
            } else if (c > 0 && c < 254) {
                message[count] = c;
                ++count;
            }   
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
        printf("Message: %s\n", message);
        // Inserindo mensagem no JSON
        cJSON * message_json;
        message_json = cJSON_CreateString((const char *) message);
        cJSON_AddItemToObject(packet, "Message", message_json);

        data = cJSON_Print(packet);

        // Enviando pacote com 'Voltage' e 'Message' em JSON
        esp_mqtt_client_publish(client, "esp32/output/", data, 0, 1, 0);
        vTaskDelay(oneSecond * 5);
        ESP_LOGI(TAG, "sent publish successful");
        
        msg_id = esp_mqtt_client_subscribe(client, "esp32/input/", 0);
        ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);
        
        char data_str[255] = "";
        sprintf(data_str, "%.*s", event->data_len, event->data);

        char topic_str[255] = "";
        sprintf(topic_str, "%.*s", event->topic_len, event->topic);

        // Verifica o topico que a mensagem foi recebida
        if (strcmp(topic_str, "esp32/input/") == 0) {
            // Verifica se a mensagem é de mudança de LED
            if (strcmp(data_str, "LED") == 0) {
                ESP_LOGI("/esp32/input/", "Alterou o estado do LED");
                gpio_set_level(BLINK_GPIO, s_led_state);
                s_led_state = !s_led_state;
                vTaskDelay(oneSecond * 3);
            // Ou se foi mensagem de SOS
            } else if (strcmp(data_str, "SOS") == 0) {
                ESP_LOGI("/esp32/input/", "ALERTA - SOS");
                sos_flash(); 
            }
        }

        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

// Funcao para configurar GPIOs e MQTT
static void app_start(void)
{   
    // Configuracoes do potenciometro (GPIO36)
    // ADC1 no Canal 0 -> Atenuacao de 11dB para ampliar a amostra
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_DEFAULT, 0, &adc1_chars);
    ESP_ERROR_CHECK(adc1_config_width(ADC_WIDTH_BIT_DEFAULT));
    ESP_ERROR_CHECK(adc1_config_channel_atten(ADC1_CHANNEL_0, ADC_ATTEN_DB_11));

    // Definicoes do LED
    gpio_reset_pin(BLINK_GPIO);
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);

    // Configuracoes MQTT
    esp_mqtt_client_config_t mqtt_cfg = {
        .uri = CONFIG_BROKER_URL,   // URL definida no arquivo de configuracao (sdkconfig)
    };
    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
}

void app_main(void)
{
    // ========= INICIALIZANDO MEMORIA DO ESP32 ========= //
    // Adquirido do exemplo /examples/protocols/mqtt/tcp/main/app_main.c
    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("MQTT_CLIENT", ESP_LOG_VERBOSE);
    esp_log_level_set("MQTT_EXAMPLE", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT_BASE", ESP_LOG_VERBOSE);
    esp_log_level_set("esp-tls", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT", ESP_LOG_VERBOSE);
    esp_log_level_set("OUTBOX", ESP_LOG_VERBOSE);

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Credenciais de Wifi (sdkconfig)
    ESP_ERROR_CHECK(example_connect());

    // Iniciando configuracoes de GPIOs e MQTT
    app_start();
}