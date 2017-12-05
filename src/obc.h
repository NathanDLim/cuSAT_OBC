/*
 * obc.h
 *
 *  Created on: Oct 6, 2017
 *      Author: admin
 */


// choose whether system goes into debug mode or not
#define _DEBUG

/*
 * Bit selection.
 * BIT(0) = 0b0001
 * BIT(1) = 0b0010
 * BIT(2) = 0b0100
 * ... etc
 */
#define BIT(x) (1 << (x))

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

/* List of all possible Satellite modes */
enum mode {
	INITIAL,
	SUN_POINTING,
	NADIR_POINTING,
	SAFETY,
	NUM_MODES,
};

/* Our global obc variable */
struct obc obc;

void obc_main(void);

