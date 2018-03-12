/*
 * Copyright (c) 2017, NXP Semiconductor, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * o Redistributions of source code must retain the above copyright notice, this list
 *   of conditions and the following disclaimer.
 *
 * o Redistributions in binary form must reproduce the above copyright notice, this
 *   list of conditions and the following disclaimer in the documentation and/or
 *   other materials provided with the distribution.
 *
 * o Neither the name of NXP Semiconductor, Inc. nor the names of its
 *   contributors may be used to endorse or promote products derived from this
 *   software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
 
/**
 * @file    alarm.c
 * @brief   Application entry point.
 */
#include <stdio.h>
#include "board.h"
#include "peripherals.h"
#include "pin_mux.h"
#include "clock_config.h"
#include "MK64F12.h"
#include "fsl_debug_console.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "queue.h"
#include "event_groups.h"

#define LIMIT_TIME		(60)
#define LIMIT_HOUR		(24)
#define TICKS_SECONDS	(1000)
#define EVENT_SECONDS (1<<0)
#define EVENT_MINUTES (1<<1)
#define EVENT_HOURS	  (1<<2)
#define QUEUE_ELEMENTS	(3)

SemaphoreHandle_t minutes_semaphore;
SemaphoreHandle_t hours_semaphore;
EventGroupHandle_t g_time_events;
QueueHandle_t xQueue;

typedef enum{seconds_type, minutes_type, hours_type} time_types_t;

typedef struct
{
	time_types_t time_type;
    uint8_t value;
} time_msg_t;

typedef struct
{
	uint8_t seconds;
	uint8_t minutes;
	uint8_t hours;
} alarm_t;

alarm_t alarm = {1, 0, 0};

void seconds_task(void *arg)
{
	TickType_t xLastWakeTime;
	const TickType_t xPeriod = pdMS_TO_TICKS(TICKS_SECONDS);
	xLastWakeTime = xTaskGetTickCount();

	uint8_t seconds = 0;
	time_msg_t *time_queue;

	time_queue = pvPortMalloc(sizeof(time_msg_t));
	time_queue->time_type = seconds_type;

	for(;;)
	{
		vTaskDelayUntil(&xLastWakeTime, xPeriod);
		seconds++;
		time_queue->value = seconds;
		if(LIMIT_TIME == seconds)
		{
			seconds = 0;
			xSemaphoreGive(minutes_semaphore);
		}
		if(seconds == alarm.seconds)
		{
			xEventGroupSetBits(g_time_events, EVENT_SECONDS);
		}
		xQueueSend(xQueue, &time_queue, portMAX_DELAY);
	}
}

void minutes_task(void *arg)
{
	uint8_t minutes = 0;
	time_msg_t *time_queue;

	time_queue = pvPortMalloc(sizeof(time_msg_t));
	time_queue->time_type = minutes_type;

	for(;;)
	{
		xSemaphoreTake(minutes_semaphore,portMAX_DELAY);
		minutes++;
		time_queue->value = minutes;
		if(LIMIT_TIME == minutes)
		{
			minutes = 0;
			xSemaphoreGive(hours_semaphore);
		}
		xSemaphoreGive(minutes_semaphore);

		if(minutes == alarm.minutes)
		{
			xEventGroupSetBits(g_time_events, EVENT_MINUTES);
		}
		xQueueSend(xQueue, &time_queue, portMAX_DELAY);
	}
}

void hours_task(void *arg)
{
	uint8_t hours = 0;
	time_msg_t *time_queue;

	time_queue = pvPortMalloc(sizeof(time_msg_t));
	time_queue->time_type = hours_type;

	for(;;)
	{
		xSemaphoreTake(hours_semaphore,portMAX_DELAY);
		hours++;

		if(LIMIT_HOUR == hours)
		{
			hours = 0;
		}
		xSemaphoreGive(hours_semaphore);

		if(hours == alarm.hours)
		{
			xEventGroupSetBits(g_time_events, EVENT_HOURS);
		}
		xQueueSend(xQueue, &time_queue, portMAX_DELAY);
	}

}

void alarm_task(void *arg)
{
	for(;;)
	{
		xEventGroupWaitBits(g_time_events, (EVENT_SECONDS|EVENT_MINUTES|EVENT_HOURS),
				pdTRUE, pdTRUE, portMAX_DELAY);

		PRINTF("ALARM\n");
	}
}

void print_task(void *arg)
{
	time_msg_t *time_queue;

	PRINTF("%d:", alarm.hours);
	PRINTF("%d:", alarm.minutes);
	PRINTF("%d", alarm.seconds);

	for(;;)
	{
		xQueueReceive(xQueue, &time_queue, portMAX_DELAY);


		vPortFree(time_queue);
	}
}

int main(void) {

  	/* Init board hardware. */
    BOARD_InitBootPins();
    BOARD_InitBootClocks();
    BOARD_InitBootPeripherals();
  	/* Init FSL debug console. */
    BOARD_InitDebugConsole();

    minutes_semaphore = xSemaphoreCreateBinary();
    hours_semaphore = xSemaphoreCreateBinary();
    g_time_events = xEventGroupCreate();

    xQueueCreate(QUEUE_ELEMENTS, sizeof(time_msg_t));
    xTaskCreate(seconds_task, "Seconds", configMINIMAL_STACK_SIZE, NULL, configMAX_PRIORITIES-2, NULL);
    xTaskCreate(minutes_task, "Minutes", configMINIMAL_STACK_SIZE, NULL, configMAX_PRIORITIES-1, NULL);
    xTaskCreate(hours_task, "Hours", configMINIMAL_STACK_SIZE, NULL, configMAX_PRIORITIES-1, NULL);
    xTaskCreate(alarm_task, "Alarm", configMINIMAL_STACK_SIZE, NULL, configMAX_PRIORITIES-1, NULL);
    xTaskCreate(print_task, "Print", configMINIMAL_STACK_SIZE, NULL, configMAX_PRIORITIES-1, NULL);
	vTaskStartScheduler();

    for(;;)
    {

    }

    return 0 ;
}
