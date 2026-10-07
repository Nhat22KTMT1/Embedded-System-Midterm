/*
 * Example 10 - Blocking when receiving from a queue
 * Ported from FreeRTOS Queue Management slides (Arduino) to ESP-IDF (VS Code)
 *
 * Idea:
 *  - Two sender tasks (lower priority, no block time) continuously write
 *    to the queue.
 *  - One receiver task (higher priority, 100ms block time) waits for data.
 *  - Because the receiver has higher priority, it always pre-empts a
 *    sender the instant data becomes available, so the queue never
 *    holds more than one item at a time.
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"


static QueueHandle_t xQueue = NULL;

/* ---------------- Sender task ---------------- */
static void vSenderTask(void *pvParameters)
{
    int32_t lValueToSend = (int32_t)(intptr_t)pvParameters;
    BaseType_t xStatus;

    printf("lValueToSend = %ld\n", lValueToSend);

    for (;;)
    {
        xStatus = xQueueSendToBack(xQueue, &lValueToSend, 0);

        if (xStatus != pdPASS)
        {
            printf("Could not send to the queue.\n");
        }
    }
}

static void vReceiverTask(void *pvParameters)
{
    int32_t lReceivedValue;
    BaseType_t xStatus;
    const TickType_t xTicksToWait = pdMS_TO_TICKS(100);

    for (;;)
    {
        if (uxQueueMessagesWaiting(xQueue) != 0)
        {
            printf("Queue should have been empty!\n");
        }

        xStatus = xQueueReceive(xQueue, &lReceivedValue, xTicksToWait);

        if (xStatus == pdPASS)
        {
            printf("Received = %ld\n", lReceivedValue);
        }
        else
        {
            printf("Could not receive from the queue.\n");
        }
    }
}

void app_main(void)
{
    xQueue = xQueueCreate(5, sizeof(int32_t));

    if (xQueue != NULL)
    {
        xTaskCreate(vSenderTask, "Sender1", 2048, (void *)100, 1, NULL);
        xTaskCreate(vSenderTask, "Sender2", 2048, (void *)200, 1, NULL);

        xTaskCreate(vReceiverTask, "Receiver", 2048, NULL, 2, NULL);

    }
    else
    {
        printf("The queue could not be created.\n");
    }
}