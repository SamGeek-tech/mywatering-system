#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_mesh.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

static const char *TAG = "mesh_node";

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

    ESP_LOGI(TAG, "Starting simple ESP-MESH node (sample)");

    // Basic mesh configuration
    mesh_cfg_t mesh_cfg = MESH_INIT_CONFIG_DEFAULT();
    mesh_cfg.channel = 0; // auto
    mesh_cfg.router.ssid_len = strlen("your-router-ssid");
    memcpy(mesh_cfg.router.ssid, "your-router-ssid", mesh_cfg.router.ssid_len);
    memcpy(mesh_cfg.router.password, "your-router-password", strlen("your-router-password"));

    ESP_ERROR_CHECK(esp_mesh_set_config(&mesh_cfg));

    // Start mesh
    ESP_ERROR_CHECK(esp_mesh_start());

    while (1) {
        ESP_LOGI(TAG, "Mesh node running...");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}
