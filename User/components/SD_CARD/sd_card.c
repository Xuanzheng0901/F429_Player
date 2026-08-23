#include "sd_card.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "LOG.h"
#include "bsp_driver_sd.h"
#include "fatfs.h"
#include "sdio.h"

#define SD_CARD_TASK_STACK_DEPTH       1024U
#define SD_CARD_TASK_PRIORITY          10U
#define SD_CARD_POLL_PERIOD_MS         50U
#define SD_CARD_DEBOUNCE_SAMPLE_COUNT  3U
#define SD_TREE_MAX_DEPTH              8U
#define SD_TREE_PATH_CAPACITY          512U
#define SD_TREE_NAME_UTF8_SIZE         192U
#define SD_TREE_LINE_SIZE              224U

static const TCHAR s_sd_volume[] = {(TCHAR)'0', (TCHAR)':', 0};
static TCHAR s_tree_path[SD_TREE_PATH_CAPACITY] = {(TCHAR)'0', (TCHAR)':', (TCHAR)'/', 0};
static DIR s_directory_stack[SD_TREE_MAX_DEPTH];
static FILINFO s_file_info_stack[SD_TREE_MAX_DEPTH];
static TaskHandle_t s_sd_card_task_handle = NULL;

static size_t tchar_length(const TCHAR *text)
{
    size_t length = 0;
    while(text[length] != 0)
        length++;
    return length;
}

static size_t utf8_sequence_length(uint32_t codepoint)
{
    if(codepoint <= 0x7FU)
        return 1U;
    if(codepoint <= 0x7FFU)
        return 2U;
    if(codepoint <= 0xFFFFU)
        return 3U;
    return 4U;
}

static size_t append_utf8(char *output, size_t offset, uint32_t codepoint)
{
    if(codepoint <= 0x7FU)
    {
        output[offset++] = (char)codepoint;
    }
    else if(codepoint <= 0x7FFU)
    {
        output[offset++] = (char)(0xC0U | (codepoint >> 6U));
        output[offset++] = (char)(0x80U | (codepoint & 0x3FU));
    }
    else if(codepoint <= 0xFFFFU)
    {
        output[offset++] = (char)(0xE0U | (codepoint >> 12U));
        output[offset++] = (char)(0x80U | ((codepoint >> 6U) & 0x3FU));
        output[offset++] = (char)(0x80U | (codepoint & 0x3FU));
    }
    else
    {
        output[offset++] = (char)(0xF0U | (codepoint >> 18U));
        output[offset++] = (char)(0x80U | ((codepoint >> 12U) & 0x3FU));
        output[offset++] = (char)(0x80U | ((codepoint >> 6U) & 0x3FU));
        output[offset++] = (char)(0x80U | (codepoint & 0x3FU));
    }
    return offset;
}

static void tchar_to_utf8(const TCHAR *input, char *output, size_t output_size)
{
    size_t input_index = 0;
    size_t output_index = 0;

    if(output_size == 0U)
        return;

    while(input[input_index] != 0)
    {
        uint32_t codepoint = input[input_index++];

        if(codepoint >= 0xD800U && codepoint <= 0xDBFFU)
        {
            const uint32_t low_surrogate = input[input_index];
            if(low_surrogate >= 0xDC00U && low_surrogate <= 0xDFFFU)
            {
                input_index++;
                codepoint = 0x10000U + ((codepoint - 0xD800U) << 10U)
                            + (low_surrogate - 0xDC00U);
            }
            else
            {
                codepoint = 0xFFFDU;
            }
        }
        else if(codepoint >= 0xDC00U && codepoint <= 0xDFFFU)
        {
            codepoint = 0xFFFDU;
        }

        const size_t sequence_length = utf8_sequence_length(codepoint);
        if(output_index + sequence_length >= output_size)
            break;

        output_index = append_utf8(output, output_index, codepoint);
    }

    output[output_index] = '\0';
}

static bool is_dot_entry(const TCHAR *name)
{
    if(name[0] != (TCHAR)'.')
        return false;

    return name[1] == 0 || (name[1] == (TCHAR)'.' && name[2] == 0);
}

static void log_tree_entry(const FILINFO *file_info, uint32_t depth)
{
    char name_utf8[SD_TREE_NAME_UTF8_SIZE];
    char line[SD_TREE_LINE_SIZE];
    size_t offset = 0;

    tchar_to_utf8(file_info->fname, name_utf8, sizeof(name_utf8));

    for(uint32_t index = 0; index < depth && offset + 4U < sizeof(line); index++)
    {
        memcpy(&line[offset], "|   ", 4U);
        offset += 4U;
    }

    if(file_info->fattrib & AM_DIR)
    {
        (void)snprintf(&line[offset], sizeof(line) - offset, "|-- [D] %s/", name_utf8);
    }
    else
    {
        (void)snprintf(&line[offset], sizeof(line) - offset, "|-- [F] %s (%lu bytes)",
                       name_utf8, (unsigned long)file_info->fsize);
    }

    LOGI("SD", "%s", line);
}

static bool append_directory_to_path(TCHAR *path, size_t capacity, const TCHAR *directory_name,
                                     size_t *original_length)
{
    const size_t path_length = tchar_length(path);
    const size_t name_length = tchar_length(directory_name);
    const bool needs_separator = path_length > 0U && path[path_length - 1U] != (TCHAR)'/';
    const size_t required_length = path_length + (needs_separator ? 1U : 0U) + name_length + 1U;

    if(required_length > capacity)
        return false;

    *original_length = path_length;
    size_t offset = path_length;
    if(needs_separator)
        path[offset++] = (TCHAR)'/';

    memcpy(&path[offset], directory_name, name_length * sizeof(TCHAR));
    offset += name_length;
    path[offset] = 0;
    return true;
}

static FRESULT print_directory_tree(TCHAR *path, size_t capacity, uint32_t depth)
{
    DIR *directory = &s_directory_stack[depth];
    FILINFO *file_info = &s_file_info_stack[depth];
    FRESULT result = f_opendir(directory, path);

    if(result != FR_OK)
        return result;

    while(true)
    {
        result = f_readdir(directory, file_info);
        if(result != FR_OK || file_info->fname[0] == 0)
            break;

        if(is_dot_entry(file_info->fname))
            continue;

        log_tree_entry(file_info, depth);

        if(file_info->fattrib & AM_DIR)
        {
            if(depth + 1U >= SD_TREE_MAX_DEPTH)
            {
                LOGW("SD", "Directory depth limit reached: %lu", (unsigned long)SD_TREE_MAX_DEPTH);
                continue;
            }

            size_t original_length;
            if(!append_directory_to_path(path, capacity, file_info->fname, &original_length))
            {
                LOGW("SD", "Directory path is too long, skipping subtree");
                continue;
            }

            const FRESULT child_result = print_directory_tree(path, capacity, depth + 1U);
            path[original_length] = 0;
            if(child_result != FR_OK)
                LOGE("SD", "Failed to read subdirectory, FatFs=%d", (int)child_result);
        }
    }

    const FRESULT close_result = f_closedir(directory);
    return result == FR_OK ? close_result : result;
}

static void reset_sd_card_storage(void)
{
    if(HAL_SD_DeInit(&hsd) != HAL_OK)
        LOGW("SD", "HAL SD deinitialization failed");

    // // The generated MSP deinitialization only disables the SDIO clock.
    // // Reset the peripheral to discard stale data state and FIFO registers.
    __HAL_RCC_SDIO_FORCE_RESET();
    // __DSB();
    __HAL_RCC_SDIO_RELEASE_RESET();
    // __DSB();
    //
    HAL_NVIC_ClearPendingIRQ(SDIO_IRQn);
    HAL_NVIC_ClearPendingIRQ(DMA2_Stream3_IRQn);

    MX_SDIO_SD_Init();

    if(FATFS_UnLinkDriver(SDPath) != 0U)
    {
        retSD = 1U;
        LOGE("SD", "FatFs driver unlink failed");
        return;
    }

    retSD = FATFS_LinkDriver(&SD_Driver, SDPath);
    if(retSD != 0U)
        LOGE("SD", "FatFs driver relink failed: %u", retSD);
}

static void print_sd_card_tree(void)
{
    if(retSD != 0U)
    {
        LOGE("SD", "FatFs driver is unavailable: %u", retSD);
        return;
    }

    LOGI("SD", "Mounting FAT32 volume");
    FRESULT result = f_mount(&SDFatFS, s_sd_volume, 1U);
    if(result != FR_OK)
    {
        LOGE("SD", "Mount failed, FatFs=%d", (int)result);
        return;
    }

    LOGI("SD", "0:/");
    result = print_directory_tree(s_tree_path, SD_TREE_PATH_CAPACITY, 0U);
    if(result == FR_OK)
        LOGI("SD", "Directory tree completed");
    else
        LOGE("SD", "Directory traversal failed, FatFs=%d", (int)result);

    const FRESULT unmount_result = f_mount(NULL, s_sd_volume, 1U);
    if(unmount_result != FR_OK)
        LOGE("SD", "Unmount failed, FatFs=%d", (int)unmount_result);
}

static bool sd_card_is_present(void)
{
    return BSP_SD_IsDetected() == SD_PRESENT;
}

static void sd_card_monitor_task(void *argument)
{
    (void)argument;

    if(retSD != 0U)
    {
        LOGE("SD", "FatFs driver link failed: %u", retSD);
        s_sd_card_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    bool stable_present = sd_card_is_present();
    bool candidate_present = stable_present;
    uint32_t candidate_sample_count = 0U;

    if(stable_present)
    {
        LOGI("SD", "SD card present at startup");
        print_sd_card_tree();
    }
    else
    {
        LOGI("SD", "No SD card, waiting for insertion");
    }

    while(true)
    {
        vTaskDelay(pdMS_TO_TICKS(SD_CARD_POLL_PERIOD_MS));

        const bool sampled_present = sd_card_is_present();
        if(sampled_present == stable_present)
        {
            candidate_present = stable_present;
            candidate_sample_count = 0U;
            continue;
        }

        if(sampled_present != candidate_present)
        {
            candidate_present = sampled_present;
            candidate_sample_count = 1U;
            continue;
        }

        candidate_sample_count++;
        if(candidate_sample_count < SD_CARD_DEBOUNCE_SAMPLE_COUNT)
            continue;

        stable_present = candidate_present;
        candidate_sample_count = 0U;

        if(stable_present)
        {
            LOGI("SD", "SD card inserted");
            print_sd_card_tree();
        }
        else
        {
            LOGW("SD", "SD card removed");
            const FRESULT unmount_result = f_mount(NULL, s_sd_volume, 1U);
            if(unmount_result != FR_OK)
                LOGE("SD", "Unmount after removal failed, FatFs=%d", (int)unmount_result);

            reset_sd_card_storage();
        }
    }
}

void sd_card_monitor_start(void)
{
    if(s_sd_card_task_handle != NULL)
    {
        LOGW("SD", "SD card monitor is already running");
        return;
    }

    const BaseType_t result = xTaskCreate(sd_card_monitor_task, "sd_card", SD_CARD_TASK_STACK_DEPTH,
                                           NULL, SD_CARD_TASK_PRIORITY, &s_sd_card_task_handle);
    if(result != pdPASS)
    {
        s_sd_card_task_handle = NULL;
        LOGE("SD", "Failed to create SD card monitor task");
    }
}
