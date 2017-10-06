/*
 * adc.c
 *
 *  Created on: Oct 6, 2017
 *      Author: admin
 */

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

#define ADC_DELAY 1000

void task_adc(void *arg) {

	for (;;) {


		vTaskDelay(ADC_DELAY);
	}
}
