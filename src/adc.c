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

void task_attitude(void *arg)
{
	for (;;) {
		printf("doing attitude control\n");
		vTaskDelay(1);
	}
}

void task_detumble(void *arg)
{
	for (;;) {
		printf("doing detumbling\n");
		vTaskDelay(10);
	}
}

void task_rebase_adc(void *arg)
{
	for (;;) {
		printf("rebasing ADC from GPS data\n");
		vTaskSuspend(NULL);
	}
}
