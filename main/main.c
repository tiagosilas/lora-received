#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "mqtt_client.h"
#include "lora.h" // Sua biblioteca LoRa
#include "mbedtls/base64.h"

// Configurações da rede Wi-Fi
#define WIFI_SSID     "2G_AERIS"
#define WIFI_PASSWORD "aeris2021"

// Define um grupo de evento para aguardar a conexão
#define WIFI_CONNECTED_BIT BIT0
static EventGroupHandle_t wifi_event_group;

// Buffer para pacotes LoRa
uint8_t buf[64];

// Cliente MQTT
esp_mqtt_client_handle_t client;

static const char *TAG = "MAIN";

/**
 * @brief Decodifica uma string Base64 para bytes.
 * @param out_buffer Buffer de saída (já alocado)
 * @param in_str String Base64 de entrada
 * @param out_len Tamanho do buffer de saída
 * @return Número de bytes decodificados ou -1 se falhar
 */
int decode_base64(uint8_t *out_buffer, size_t out_len, const char *in_str)
{
    if (!in_str || !out_buffer) {
        return -1;
    }

    size_t in_len = strlen(in_str);
    size_t decoded_len = 0;

    int ret = mbedtls_base64_decode(out_buffer, out_len, &decoded_len,
                                     (const unsigned char *)in_str, in_len);

    if (ret != 0) {
        ESP_LOGE(TAG, "Falha na decodificação Base64 (erro: -0x%x)", -ret);
        return -1;
    }

    return decoded_len;
}

// Manipulador de eventos de Wi-Fi
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "Desconectado do Wi-Fi. Tentando reconectar...");
        esp_wifi_connect();
        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Conectado com IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// Função para conectar ao Wi-Fi
static void wifi_init(void)
{
    wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Conectando ao Wi-Fi...");

    EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
                                           WIFI_CONNECTED_BIT,
                                           pdFALSE,
                                           pdTRUE,
                                           portMAX_DELAY);
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Wi-Fi conectado com sucesso");
    } else {
        ESP_LOGE(TAG, "Falha ao conectar no Wi-Fi");
    }
}

// Configuração do cliente MQTT
void mqtt_app_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.transport = MQTT_TRANSPORT_OVER_TCP,
        .broker.address.hostname = "mqtt.aerisiot.com",  // Altere conforme seu broker
        .broker.address.port = 1883,
        .credentials.client_id = "esp32-lora-gateway",
    };

    client = esp_mqtt_client_init(&mqtt_cfg);
    if (client != NULL) {
        esp_mqtt_client_start(client);
        ESP_LOGI(TAG, "MQTT iniciado com sucesso");
    } else {
        ESP_LOGE(TAG, "Falha ao inicializar cliente MQTT");
    }
}

void task_rx(void *p)
{
    uint8_t buf[64];         // Dados brutos recebidos
    uint8_t decoded_buf[64]; // Onde vai ficar o payload decodificado
    int x;

    for (;;) {
        lora_receive(); // Coloca o rádio em modo RX

        while (lora_received()) {
            x = lora_receive_packet(buf, sizeof(buf));

            if (x > 0) {
                buf[x] = '\0'; // Finaliza como string para Base64
                ESP_LOGI(TAG, "Dados brutos recebidos: %s", buf);
                ESP_LOG_BUFFER_HEX(TAG, buf, x);

                // Tenta decodificar Base64
                int decoded_len = decode_base64(decoded_buf, sizeof(decoded_buf), (char*)buf);
                if (decoded_len > 0) {
                    decoded_buf[decoded_len] = '\0';
                    ESP_LOGI(TAG, "Dados decodificados: %s", decoded_buf);

                    // Publica no MQTT
                    if (client != NULL) {
                        esp_mqtt_client_publish(client, "lora/data", (char*)decoded_buf, decoded_len, 1, 0);
                    }
                } else {
                    ESP_LOGW(TAG, "Não foi possível decodificar Base64");
                }
            } else {
                ESP_LOGW(TAG, "Nenhum dado válido recebido");
            }

            vTaskDelay(500 / portTICK_PERIOD_MS);
        }

        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

// // Tarefa principal: escuta LoRa e envia os dados via MQTT
// void task_rx(void *p)
// {
//     int x;
//     for (;;) {
//         lora_receive(); // Coloca o módulo LoRa em modo de recepção

//         while (lora_received()) {
//             x = lora_receive_packet(buf, sizeof(buf) - 1); // Deixa espaço para '\0'
//             buf[x] = '\0'; // Finaliza como string

//             ESP_LOGI(TAG, "Pacote recebido: %s", buf);

//             // Publica no tópico MQTT
//             if (client != NULL) {
//                 esp_mqtt_client_publish(client, "lora/data", (char*)buf, x, 1, 0);
//             }

//             vTaskDelay(500 / portTICK_PERIOD_MS); // Pequeno delay
//         }

//         vTaskDelay(10 / portTICK_PERIOD_MS);
//     }
// }

// Função principal
void app_main()
{
    ESP_LOGI(TAG, "Inicializando...");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Inicializar LoRa
    lora_init();
    lora_set_frequency(915e6);
    lora_set_bandwidth(125e3);
    lora_set_spreading_factor(12);
    lora_set_coding_rate(5); // 4/5
    lora_explicit_header_mode();
    lora_disable_crc();
    ESP_LOGI(TAG, "LoRa inicializado");

    // Inicializar Wi-Fi
    wifi_init();

    // Inicializar MQTT
    mqtt_app_start();

    // Criar tarefa para receber dados LoRa
    xTaskCreate(&task_rx, "task_rx", 4096, NULL, 5, NULL);
}