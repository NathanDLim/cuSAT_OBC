/*
 * housekeeping.c
 *
 *  Created on: Oct 17, 2017
 *      Author: admin
 */

#include <FreeRTOS/FreeRTOS.h>
#include <FreeRTOS/task.h>
#include <stdio.h>

void task_housekeep(void *arg)
{
	const int delay = 1000/portTICK_RATE_MS;//(300000 / portTICK_RATE_MS) // every 5 minutes

	for (;;) {
		printf("Now performing Housekeeping task\n");
		vTaskDelay(delay);
	}

	for (;;) {
		vTaskDelay(10);
	}
}
