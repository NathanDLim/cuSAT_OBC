/*
 * gps.c
 *
 *  Created on: Oct 17, 2017
 *      Author: admin
 */

#include <FreeRTOS/FreeRTOS.h>
#include <FreeRTOS/task.h>

void task_gps(void *arg)
{
	for (;;) {
		printf("GPS task running\n");
		vTaskDelay(1000);
	}
}
