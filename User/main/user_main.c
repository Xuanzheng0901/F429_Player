#include "ff.h"
#include "FreeRTOS.h"
#include "main.h"
#include "task.h"
#include "LOG.h"

static void led_blink_task(void* pvParameter)
{
    LOGI("LED", "Stack addr: %p", xTaskGetCurrentTaskHandle());
    HAL_GPIO_TogglePin(LED_B_GPIO_Port, LED_B_Pin);
    vTaskDelay(500);
}

void app_main(void)
{
    log_init(LOG_INFO);
    xTaskCreate(led_blink_task, "LED", 512, NULL, 15, NULL);

    vTaskDelay(100);

}