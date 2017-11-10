/*
 * obc.c
 *
 *  Created on: Oct 6, 2017
 *      Author: Nathan Lim
 */

#include <hal/Utility/util.h>

#include <FreeRTOS/FreeRTOS.h>
#include <FreeRTOS/task.h>
#include "housekeep.h"
#include "adc.h"
#include "obc.h"
#include "gps.h"

#define HIGH_PRIORITY		configMAX_PRIORITIES-1
#define MEDIUM_PRIORITY		configMAX_PRIORITIES-2
#define LOW_PRIORITY		configMAX_PRIORITIES-4

/* List of Priorities for all tasks on the system */
#define HOUSEKEEP_PRIORITY 		MEDIUM_PRIORITY
#define MODESWITCH_PRIORITY 	LOW_PRIORITY
#define ERROR_PRIORITY			MEDIUM_PRIORITY		// check and handle errors
#define ATTITUDE_PRIORITY 		LOW_PRIORITY
#define DETUMBLE_PRIORITY 		MEDIUM_PRIORITY
#define REBASE_ADC_PRIORITY 	LOW_PRIORITY
#define GPS_PRIORITY 			MEDIUM_PRIORITY
#define PAYLOAD_RX_PRIORITY 	HIGH_PRIORITY 		// retrieve payload data
#define PAYLOAD_PRO_PRIORITY 	MEDIUM_PRIORITY		// process payload data
#define COMM_RX_PRIORITY 		MEDIUM_PRIORITY 	// receive and process commands
#define COMM_HANDLE_PRIORITY	MEDIUM_PRIORITY		// command timing setup
#define DATA_TX_PRIORITY 		HIGH_PRIORITY  		// frame and send data to ground
#define SD_PRIORITY				MEDIUM_PRIORITY		// send and retrieve SD card data



/* List of tasks on the system that can be turned on and off. */
enum task {
	// gather and store sensor data
	HOUSEKEEP	= 0,
	// calculate attitude controls
	ATTITUDE	= 1,
	// stabilize satellite
	DETUMBLE 	= 2,
	// rebase attitude controls from current GPS data
	REBASE_ADC	= 3,
	// gather GPS data
	GPS			= 4,
	// receive data from payload
	PAYLOAD_RX	= 5,
	// process payload data
	PAYLOAD_PRO = 6,
	// receive communication data
	COMM_RX		= 7,
	// handle communication commands
	COMM_HANDLE = 8,
	// send data to ground station
	DATA_TX		= 9,
	NUM_TASKS,
};

/* List of task handles. The entries match up with enum task. */
xTaskHandle task[5];

/*
 * Matrix for determining which tasks should be active during each mode.
 * The index of the masks follows enum mode defined in obc.h.
 *
 * ex. If mode_mask[1] refers to SUN_POINTING mode.
 * In order to stop task 0 from being run during sun pointing mode, and run all other tasks, we set
 * 		mode_mask[1] = 0b00000001
 *
 * The numbering of the tasks are shown in enum task, where each entry represents the bit number.
 */
const unsigned int mode_mask[] = {0b01001, 0b11011, 0b11101, 0b00001};

void mode_switching(void *arg) {
	size_t i;

	// loop forever
	for (;;) {
//		printf("now switching to mode %i\n", obc.mode);
		for (i = 0; i < ARRAY_SIZE(task); ++i) {
			if (mode_mask[obc.mode] & BIT(i)) {
				vTaskResume(task[i]);
//				printf("resume task %i\n", i + 1);
			} else {
				vTaskSuspend(task[i]);
//				printf("suspend task %i\n", i + 1);
			}
		}
//		fflush(stdout);

		// once the mode has been changed, we can suspend this task until the next time
		// we need to change modes
		vTaskSuspend(NULL);
	}
}

#ifdef _DEBUG
void task_debug(void *arg)
{
	xTaskHandle *hmode_switch = (xTaskHandle *)arg;
	unsigned int num = 0;
	for (;;) {
		printf("Debug Mode\n");
		printf("Enter a number to select the mode of operation\n");
		printf("1. Initial Mode\n");
		printf("2. Sun Pointing Mode\n");
		printf("3. Nadir Pointing Mode\n");
		printf("4. Safety Mode\n>>");
		while(UTIL_DbguGetIntegerMinMax(&num, 1, 4) == 0);
		if (num < 1 || num > 4)
			continue;
		obc.mode = num;
		vTaskResume(*hmode_switch);
	}
}
#endif

void obc_init(void)
{
	obc.mode = INITIAL;
}


void obc_main(void)
{
	obc_init();

	xTaskGenericCreate(task_housekeep, (const signed char*)"housekeep", 4096, NULL, HOUSEKEEP_PRIORITY, &task[0], NULL, NULL);
	xTaskGenericCreate(task_attitude, (const signed char*)"ADC", 4096, NULL, ATTITUDE_PRIORITY, &task[1], NULL, NULL);
	xTaskGenericCreate(task_detumble, (const signed char*)"Detumble", 4096, NULL, DETUMBLE_PRIORITY, &task[2], NULL, NULL);
	xTaskGenericCreate(task_rebase_adc, (const signed char*)"Rebase", 4096, NULL, REBASE_ADC_PRIORITY, &task[3], NULL, NULL);
	xTaskGenericCreate(task_gps, (const signed char*)"GPS", 4096, NULL, GPS_PRIORITY, &task[4], NULL, NULL);

#ifdef _DEBUG
	xTaskGenericCreate(task_debug, (const signed char*)"Debug", 4096, NULL, configMAX_PRIORITIES-2, NULL, NULL, NULL);
#endif

	vTaskStartScheduler();
}
