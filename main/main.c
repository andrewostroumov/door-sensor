#include <stdio.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

#define ESP_INTR_FLAG_DEFAULT 0
#define BUTTON_PIN 12

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

void app_main(void) {
    xSemaphore = xSemaphoreCreateBinary();

    gpio_pad_select_gpio(BUTTON_PIN);
    gpio_set_direction(BUTTON_PIN, GPIO_MODE_INPUT);
    gpio_set_intr_type(BUTTON_PIN, GPIO_INTR_NEGEDGE);

    xTaskCreate(sensor_task, "sensor_task", 2048, NULL, 10, NULL);

    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
    gpio_isr_handler_add(BUTTON_PIN, sensor_isr_handler, NULL);
}
