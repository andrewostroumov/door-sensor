#include <stdio.h>
#include <string.h>
#include <time.h>
#include <driver/gpio.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <freertos/semphr.h>

#include <esp_system.h>
#include <esp_wifi.h>
#include <esp_event_loop.h>
#include <esp_log.h>
#include <nvs_flash.h>

#include <lwip/netdb.h>
#include <lwip/sockets.h>


#define ESP_INTR_FLAG_DEFAULT 0
#define SENSOR_PIN 12

#define EVENT_DELAY 2000

static EventGroupHandle_t wifi_event_group;
const int CONNECTED_BIT = BIT0;

SemaphoreHandle_t xSemaphore = NULL;

void IRAM_ATTR sensor_isr_handler(void* arg) {
    xSemaphoreGiveFromISR(xSemaphore, NULL);
}

void cats(char **str, const char *str2) {
    char *tmp = NULL;

    if ( *str != NULL && str2 == NULL ) {
        free(*str);
        *str = NULL;
        return;
    }

    if (*str == NULL) {
        *str = calloc( strlen(str2)+1, sizeof(char) );
        memcpy( *str, str2, strlen(str2) );
    }
    else {
        tmp = calloc( strlen(*str)+1, sizeof(char) );
        memcpy( tmp, *str, strlen(*str) );
        *str = calloc( strlen(*str)+strlen(str2)+1, sizeof(char) );
        memcpy( *str, tmp, strlen(tmp) );
        memcpy( *str + strlen(*str), str2, strlen(str2) );
        free(tmp);
    }
}

void make_base_request(char **request, char *method, char *path) {
    char buf[64];
    sprintf(buf, "%s %s HTTP/1.1\n", method, path);
    cats(request, buf);
    cats(request, "Host: "CONFIG_SERVER_IP"\n");
    cats(request, "Authorization: "CONFIG_AUTH_TOKEN"\n");
    cats(request, "User-Agent: ESP32\n");
}

void make_post_request(char **request, char *path, char *body, char *type) {
    char buf[64];
    make_base_request(request, "POST", path);
    sprintf(buf, "Content-Length: %d\n", strlen(body) * sizeof(char));
    cats(request, buf);
    sprintf(buf, "Content-Type: %s\n", type);
    cats(request, buf);
    cats(request, "\n");
    cats(request, body);
    cats(request, "\n");
}

void make_get_request(char **request, char *path) {
    make_base_request(request, "GET", path);
    cats(request, "\n");
}

void send_request(char *request) {
    char recv_buf[100];
    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(strtol(CONFIG_SERVER_PORT, NULL, 10));
    server.sin_addr.s_addr = inet_addr(CONFIG_SERVER_IP);

    int s = socket(AF_INET, SOCK_STREAM, 0);
    if(s < 0) {
        printf("Unable to allocate a new socket\n");
        return;
    }
    printf("Socket allocated: %d\n", s);

    int result = connect(s, (struct sockaddr *)&server, sizeof(server));
    if(result != 0) {
        printf("Unable to connect to: %s:%s\n", CONFIG_SERVER_IP, CONFIG_SERVER_PORT);
        close(s);
        return;
    }
    printf("Connected to: %s:%s\n", CONFIG_SERVER_IP, CONFIG_SERVER_PORT);

    printf("HTTP request:\n");
    printf("--------------------------------------------------------------------------------\n");
    printf(request);
    printf("--------------------------------------------------------------------------------\n");

    result = write(s, request, strlen(request));
        if(result < 0) {
        printf("Unable to send the HTTP request\n");
        close(s);
        return;
    }
    printf("HTTP request sent\n");

    printf("HTTP response:\n");
    printf("--------------------------------------------------------------------------------\n");
    int r;
    do {
        bzero(recv_buf, sizeof(recv_buf));
        r = read(s, recv_buf, sizeof(recv_buf));
        for(int i = 0; i < r; i++) {
            putchar(recv_buf[i]);
        }
    } while(r >= sizeof(recv_buf));
    printf("--------------------------------------------------------------------------------\n");

    close(s);
    printf("Socket closed\n");
}

void send_post_request(char *path, char *body) {
    char *request = "";
    make_post_request(&request, path, body, "application/json");
    send_request(request);
}

void send_get_request(char *path) {
    char *request = "";
    make_get_request(&request, path);
    send_request(request);
}

void send_door_event_request() {
    char *body = "{\"message\":\"front_door_trigger\"}";
    send_post_request("/events", body);
}

void sensor_task(void* arg) {
    TickType_t last_tick = xTaskGetTickCount();
    TickType_t tick;
    for(;;) {
        if(xSemaphoreTake(xSemaphore, portMAX_DELAY) == pdTRUE) {
            tick = xTaskGetTickCount();
            if ((tick - last_tick) * portTICK_RATE_MS < EVENT_DELAY) {
                continue;
            } else {
                last_tick = tick;
            }

            printf("Front door opened\n");
            EventBits_t wifi_bits;
            wifi_bits = xEventGroupGetBits(wifi_event_group);
            if (wifi_bits & CONNECTED_BIT) {
                send_door_event_request();
            }
        }
    }
}

void wifi_task(void *pvParameter) {
    printf("Connecting to %s\n", CONFIG_WIFI_SSID);
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
    xSemaphore = xSemaphoreCreateBinary();

    gpio_pad_select_gpio(SENSOR_PIN);
    gpio_set_direction(SENSOR_PIN, GPIO_MODE_INPUT);
    gpio_set_intr_type(SENSOR_PIN, GPIO_INTR_NEGEDGE);

    xTaskCreate(sensor_task, "sensor_task", 4096, NULL, 10, NULL);

    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
    gpio_isr_handler_add(SENSOR_PIN, sensor_isr_handler, NULL);

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
            .ssid = CONFIG_WIFI_SSID,
            .password = CONFIG_WIFI_PASSWORD,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    xTaskCreate(&wifi_task, "wifi_task", 4096, NULL, 5, NULL);
}
