#include <stdio.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "freertos/event_groups.h"
#include <freertos/semphr.h>
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "nvs_flash.h"


#define ESP_INTR_FLAG_DEFAULT 0
#define SENSOR_PIN 12

#define WIFI_SSID "Xiaomi"
#define WIFI_PASS "samsung7"

static EventGroupHandle_t wifi_event_group;
const int CONNECTED_BIT = BIT0;

SemaphoreHandle_t xSemaphore = NULL;

void IRAM_ATTR sensor_isr_handler(void* arg) {
    xSemaphoreGiveFromISR(xSemaphore, NULL);
}

void sensor_task(void* arg) {
    for(;;) {
        if(xSemaphoreTake(xSemaphore, portMAX_DELAY) == pdTRUE) {
            printf("Door opened!\n");
        }
    }
}

void wifi_task(void *pvParameter) {
    printf("Connecting to %s\n", WIFI_SSID);
    printf("Waiting for connection to the wifi network...\n");
    xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, false, true, portMAX_DELAY);
    printf("Connected!\n");

    while(1) {
        vTaskDelay(1000 / portTICK_RATE_MS);
    }
}

static esp_err_t event_handler(void *ctx, system_event_t *event) {
    printf("Event: %i\n", event->event_id);
    switch(event->event_id) {
        case SYSTEM_EVENT_STA_START:
            esp_wifi_connect();
            break;
        case SYSTEM_EVENT_STA_GOT_IP:
            xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
            break;
        case SYSTEM_EVENT_STA_DISCONNECTED:
            xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
            break;
        default:
            break;
    }
    return ESP_OK;
}

void app_main(void) {
    // init sensor
    xSemaphore = xSemaphoreCreateBinary();

    gpio_pad_select_gpio(SENSOR_PIN);
    gpio_set_direction(SENSOR_PIN, GPIO_MODE_INPUT);
    gpio_set_intr_type(SENSOR_PIN, GPIO_INTR_NEGEDGE);

    xTaskCreate(sensor_task, "sensor_task", 2048, NULL, 10, NULL);

    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
    gpio_isr_handler_add(SENSOR_PIN, sensor_isr_handler, NULL);


    // init wifi
    esp_log_level_set("wifi", ESP_LOG_NONE);
    setvbuf(stdout, NULL, _IONBF, 0);
    wifi_event_group = xEventGroupCreate();

    nvs_flash_init();
    tcpip_adapter_init();
    ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL));

    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_config));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    xTaskCreate(&wifi_task, "wifi_task", 2048, NULL, 5, NULL);
}
