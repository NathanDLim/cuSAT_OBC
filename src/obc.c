/*
 * obc.c
 *
 *  Created on: Oct 6, 2017
 *      Author: Nathan Lim
 */

#include <hal/Utility/util.h>

#include <FreeRTOS.h>
#include <task.h>
#include <queue.h>

#include "obc.h"
#include "comm.h"
#include "housekeep.h"
#include "adc.h"
#include "gps.h"
#include "fram.h"
#include "file_writer.h"

#include <stdio.h>

#define HIGH_PRIORITY		configMAX_PRIORITIES-1
#define MEDIUM_PRIORITY		configMAX_PRIORITIES-2
#define LOW_PRIORITY		configMAX_PRIORITIES-4

/* List of Priorities for all tasks on the system */
#define HOUSEKEEP_PRIORITY 		MEDIUM_PRIORITY
#define MODESWITCH_PRIORITY 	LOW_PRIORITY
#define ERROR_PRIORITY			MEDIUM_PRIORITY		// check and handle errors
#define ATTITUDE_PRIORITY 		LOW_PRIORITY
#define GPS_PRIORITY 			MEDIUM_PRIORITY
#define PAYLOAD_PRO_PRIORITY 	MEDIUM_PRIORITY		// process payload data
#define COMM_RX_PRIORITY 		MEDIUM_PRIORITY 	// receive and process commands
#define COMM_HANDLE_PRIORITY	MEDIUM_PRIORITY		// command timing setup
#define COMM_PRIORITY 			HIGH_PRIORITY  		// frame and send data to ground
#define FILE_W_PRIORITY 		MEDIUM_PRIORITY		// write data to file
#define SD_PRIORITY				MEDIUM_PRIORITY		// send and retrieve SD card data

/* List of all possible Satellite modes */
enum mode {
	MODE_INITIAL,
	MODE_SUN_POINTING,
	MODE_NADIR_POINTING,
	MODE_SAFETY,
	MODE_MAX_NUM,
};

/* List of task handles. The entries match up with enum task. */
xTaskHandle task[9];

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
const unsigned int mode_mask[] = {
									0b100000000,	// Initial Mode
									0b0010011,	// Sun-pointing
									0b1110100,	// Nadir-pointing
									0b0001000		// Safety
								 };

struct obc {
	/* Debugging list of obc_commands */
	struct obc_command command_list[10];
	// The number of commands in the command list
	int command_num;

	// Current mode of the satellite
	enum mode mode;
	// The last GPS update
	long gps_time;
	// The tick count at the GPS update time
	int gps_tick;
	// TODO: add GPS position data at last update
};

static struct obc obc;

xQueueHandle adc_queue, gps_queue, file_w_queue, comm_tx_queue;

/*
 * Task for switching which mode the satellite is in.
 *
 * It suspends all of the tasks that are not needed in that mode.
 */
void mode_switching(void _UNUSED *arg) {
	size_t i;

	// loop forever
	for (;;) {
		debug("now switching to mode %i\n", obc.mode);
		for (i = 0; i < ARRAY_SIZE(task); ++i) {
			if (task[i] == 0)
				continue;
			else if (mode_mask[obc.mode] & BIT(i)) {
				vTaskResume(task[i]);
			} else {
				vTaskSuspend(task[i]);
			}
		}

		// once the mode has been changed, we can suspend this task until the next time
		// we need to change modes
		vTaskSuspend(NULL);
	}
}

/*
 * Function to sort the obc commands in order of execution time
 * TODO: Remove duplicate ID numbers
 */
void sort_command_list()
{
	int i, flag = 1;
	struct obc_command tmp;

	/* Simple bubble sort to sort commands in terms of execution time */
	while(flag != 0) {
		flag = 0;
		for (i = 0; i < obc.command_num - 1; i++) {
			if (obc.command_list[i].execution_time > obc.command_list[i + 1].execution_time) {
				tmp = obc.command_list[i];
				obc.command_list[i] = obc.command_list[i + 1];
				obc.command_list[i + 1] = tmp;
				flag = 1;
			}

		}
	}

	// debug print the command list
//	for (i = 0; i < obc.command_num; i++) {
//		printf("cmd type: %i, time:%li, data:%i\n", obc.command_list[i].id, obc.command_list[i].execution_time, obc.command_list[i].data);
//	}
//	fflush(stdout);
}

/*
 * Performs operations necessary for each obc command
 */
int execute_obc_command(size_t cmd, long option)
{
	size_t task_id = cmd >> OBC_ID_TASK_BIT;
	debug("cmd = %x, cmd-1 = %x\n", cmd, task_id);
	// Check if there is more than one task bit sent
	if ((task_id & (task_id - 1)) != 0) {
		error("Error: More than one task bit set\n");
		return -1;
	}

	struct queue_message message;
	// The message ID being sent to the task is the bottom part of the command ID
	message.id = cmd & (OBC_ID_TASK_BIT - 1);
	message.data = option;

	switch (task_id) {
		// Command handling command
		case BIT(CONTROL):
			if (xQueueSend(adc_queue, (void *) &message, 0) == pdFALSE) {
				error("Error sending adc queue message\n");
				return -1;
			}
			// send message to adc to new target
			break;
		// ADC command
		case BIT(ADC):
			// check that the command number is acceptable by adc
			if (message.id >= ADC_CMD_MAX)
				return -1;

			if (xQueueSend(adc_queue, (void *) &message, 0) == pdFALSE) {
				error("Error sending adc queue message\n");
				return -1;
			}
			break;
		case BIT(COMM_TX):
			if (xQueueSend(comm_tx_queue, (void *) &message, 0) == pdFALSE) {
				error("Error sending comm tx queue message\n");
				return -1;
			}
			break;
		default:
			return -1;
	}
	return 0;

}

/*
 * Task to execute obc commands at the appropriate time.
 *
 * TODO: There should be some obc_commands added in to do clean up and data management from time to time.
 */
void task_command_handler(void _UNUSED *arg)
{
	struct obc_command cmd;
	struct gps_queue_message message;

	// Main loop
	for (;;) {

		// read the command stack pointer and grab the next command
		if (obc.command_num == 0) {
			debug("no tasks to run\n");
			fflush(stdout);
			vTaskDelay(5000);
			continue;
		}
		cmd = obc.command_list[0];

		// if the command should be run now or delayed
		long timestamp = gps_get_timestamp();
		if (cmd.execution_time > timestamp) {
			vTaskDelay((cmd.execution_time - timestamp) / portTICK_RATE_MS);
			continue;
		}

		if (execute_obc_command(cmd.id, cmd.data) != 0){
			error("error executing command\n");
		}

		// remove the command just executed
		obc.command_list[0] = obc.command_list[obc.command_num - 1];
		obc.command_num--;

		// sort the command list
		sort_command_list();

		//vTaskDelay(100);
	}
}

#ifdef _DEBUG
void task_debug(void _UNUSED *arg)
{
	//xTaskHandle *hmode_switch = (xTaskHandle *)arg;
	unsigned int num = 0;
	struct queue_message message;

	for (;;) {
		/*
		printf("Debug Mode\n");
		printf("Enter a number to select the mode of operation\n");
		printf("1. Initial Mode\n");
		printf("2. Sun Pointing Mode\n");
		printf("3. Nadir Pointing Mode\n");
		printf("4. Safety Mode\n>> ");
		fflush(stdout);
		*/
		//while(UTIL_DbguGetIntegerMinMax(&num, 1, 4) == 0);

		//scanf("%d", &num);

		num = num > 4 ? 1 : num + 1;
		if (num < 1 || num > 4)
			continue;
		//obc.mode = num - 1;

		// test message
		message.id = 101;
		message.data = num;

		if (xQueueSend(adc_queue, (void *) &message, 0) == pdFALSE)
			error("Error sending adc queue message\n");

		debug("sending num = %i\n", num);
		fflush(stdout);

		//vTaskResume(*hmode_switch);
		vTaskDelay(2000);
	}
}
#endif

/*
 * Initializes the obc structure
 */
void obc_init(void)
{
	gps_init();
	obc.mode = 1;

	/* This part is all for debugging */
	long start = gps_get_timestamp();

	/* List of test obc_commands */
	obc.command_list[0].id = (BIT(ADC) << OBC_ID_TASK_BIT) | 0;
	obc.command_list[0].data = 10;
	obc.command_list[0].execution_time = start + 2000;

	obc.command_list[1].id = (BIT(COMM_TX) << OBC_ID_TASK_BIT) | 0;
	obc.command_list[1].data = 43 << 24;
	obc.command_list[1].execution_time = start + 4000;

	obc.command_list[2].id = (BIT(ADC) | BIT(CONTROL)) << OBC_ID_TASK_BIT;
	obc.command_list[2].data = 100;
	obc.command_list[2].execution_time = start + 3000;

	obc.command_list[3].id = (BIT(ADC) << OBC_ID_TASK_BIT) | 1;
	obc.command_list[3].data = 10;
	obc.command_list[3].execution_time = start + 6000;
	obc.command_num = 4;
}

/*
 * Creates all of the tasks
 */
void obc_main(void)
{
	obc_init();
//	xTaskHandle task_mode;

	/* Create queues */
	adc_queue = xQueueCreate(5, sizeof(struct queue_message));
	gps_queue = xQueueCreate(1, sizeof(struct gps_queue_message));
	file_w_queue = xQueueCreate(10, sizeof(struct file_queue_message));
	comm_tx_queue = xQueueCreate(10, sizeof(struct queue_message));


	/* Create tasks */
	xTaskCreate(task_housekeep, (const char*)"housekeep", configMINIMAL_STACK_SIZE, (void *) &file_w_queue, HOUSEKEEP_PRIORITY, &task[HOUSEKEEP]);
	xTaskCreate(task_adc, (const char*)"ADC", configMINIMAL_STACK_SIZE, (void *) &adc_queue, ATTITUDE_PRIORITY, &task[ADC]);
	xTaskCreate(task_gps, (const  char*)"GPS", configMINIMAL_STACK_SIZE, (void *) &gps_queue, GPS_PRIORITY, &task[GPS]);
	//xTaskCreate(mode_switching, (const char*)"Mode Switching", configMINIMAL_STACK_SIZE, NULL, LOW_PRIORITY, &task_mode);
	xTaskCreate(task_command_handler, (const char*)"Command Handler", configMINIMAL_STACK_SIZE, NULL, MEDIUM_PRIORITY, &task[CONTROL]);
	xTaskCreate(task_file_writer, (const char*)"File Writer", configMINIMAL_STACK_SIZE, (void *) &file_w_queue, FILE_W_PRIORITY, &task[FILE_W]);
	xTaskCreate(task_comm, (const char*)"Communication Downlink", configMINIMAL_STACK_SIZE, (void *) &comm_tx_queue, COMM_PRIORITY, &task[COMM_TX]);

#ifdef _DEBUG
	//xTaskCreate(task_debug, (const char*)"Debug", configMINIMAL_STACK_SIZE, &task_mode, configMAX_PRIORITIES-2, NULL);
#endif

	vTaskStartScheduler();
}
