#include "FreeRTOS.h"
#include "main.h"
#include "task.h"
#include "LOG.h"
#include "sd_card.h"

static void led_blink_task(void* pvParameter)
{
    LOGI("LED", "Stack addr: %p", xTaskGetCurrentTaskHandle());
    while(1)
    {
        HAL_GPIO_TogglePin(LED_B_GPIO_Port, LED_B_Pin);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void app_main(void)
{
    log_init(LOG_INFO);
    xTaskCreate(led_blink_task, "LED", 512, NULL, 15, NULL);
    sd_card_tree_test_start();
}
