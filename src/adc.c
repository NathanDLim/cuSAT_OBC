/*
 * adc.c
 *
 *  Created on: Oct 6, 2017
 *      Author: admin
 */

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <stdio.h>

#include "adc.h"

xSemaphoreHandle adc_mutex;

static struct adc_data adc_data;

void init_adc()
{
	if (adc_mutex == NULL)
		return;

	adc_mutex = xSemaphoreCreateMutex();
	if (adc_mutex == NULL)
		printf("Error creating ADC mutex!");
}

void task_attitude(void *arg)
{
	fflush(stdout);
	init_adc();
	for (;;) {
		printf("doing attitude control\n");
		vTaskDelay(100);
	}
}

void task_detumble(void *arg)
{
	init_adc();
	for (;;) {
		printf("doing detumbling\n");
		vTaskDelay(1000);
	}
}

void task_rebase_adc(void *arg)
{
	init_adc();
	for (;;) {
		printf("rebasing ADC from GPS data\n");
		vTaskSuspend(NULL);
	}
}

void adc_get_data(struct adc_data data)
{
	xSemaphoreTake(&adc_mutex, portMAX_DELAY);
	memcpy(&data, &adc_data, sizeof(struct adc_data));
	xSempaphoreGive(&adc_mutex);
}

