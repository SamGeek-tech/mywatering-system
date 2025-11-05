#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_mesh.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "esp_http_client.h"

static const char *TAG = "mesh_gateway";

// IoT Hub endpoint config for gateway to forward messages
#define IOTHUB_HOST "your-iothub.azure-devices.net"
#define IOTHUB_DEVICE_ID "esp32-gateway"
#define IOTHUB_SAS_TOKEN "SharedAccessSignature sr=..."

void mesh_rx_task(void *arg)
{
    uint8_t src_addr[6];
    uint8_t data[512];
    while (1) {
        int len = esp_mesh_recv(NULL, &src_addr, data, sizeof(data), portMAX_DELAY, NULL, 0);
        if (len > 0) {
            ESP_LOGI(TAG, "Received mesh data: %.*s", len, data);
            // Forward to IoT Hub via HTTPS
            esp_http_client_config_t config = {
                .url = "https://" IOTHUB_HOST "/devices/" IOTHUB_DEVICE_ID "/messages/events?api-version=2018-06-30",
                .method = HTTP_METHOD_POST,
            };
            esp_http_client_handle_t client = esp_http_client_init(&config);
            esp_http_client_set_header(client, "Authorization", IOTHUB_SAS_TOKEN);
            esp_http_client_set_header(client, "Content-Type", "application/json");
            esp_http_client_open(client, len);
            esp_http_client_write(client, (const char *)data, len);
            int status = esp_http_client_get_status_code(client);
            ESP_LOGI(TAG, "Forwarded to IoT Hub, status: %d", status);
            esp_http_client_cleanup(client);
        }
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    tcpip_adapter_init();
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_mesh_init());

    ESP_LOGI(TAG, "Starting ESP-MESH gateway (sample)");

    mesh_cfg_t mesh_cfg = MESH_INIT_CONFIG_DEFAULT();
    mesh_cfg.channel = 0;
    mesh_cfg.router.ssid_len = strlen("your-router-ssid");
    memcpy(mesh_cfg.router.ssid, "your-router-ssid", mesh_cfg.router.ssid_len);
    memcpy(mesh_cfg.router.password, "your-router-password", strlen("your-router-password"));

    ESP_ERROR_CHECK(esp_mesh_set_config(&mesh_cfg));

    ESP_ERROR_CHECK(esp_mesh_start());

    xTaskCreate(mesh_rx_task, "mesh_rx", 4096, NULL, 5, NULL);

    while (1) {
        ESP_LOGI(TAG, "Gateway running...");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}
