/**
 * @file config.h
 * @note Written by Tibor Tusori
 *
 * This file is used to supply the servo with the necessary config parameters statically without the need for the LinuxPTP config file.
 * Currently this file uses the default values from the original config system.
 */

#ifndef __CONFIG_H_
#define __CONFIG_H_


// Global servo parameters	
								//def 		// min 		// max
#define STEP_THRESHOLD			0.0			// 0.0		DBL_MAX
#define FIRST_STEP_THRESHOLD	0.00002		// 0.0		DBL_MAX
#define MAX_FREQUENCY			900000000	// 0		INT_MAX



// PI servo parameters
									//def 	// min 			// max
#define PI_INTEGRAL_CONST  			0.0 	//	0.0			DBL_MAX
#define PI_INTEGRAL_EXPONENT  		0.4 	//	-DBL_MAX	DBL_MAX
#define PI_INTEGRAL_NORM_MAX  		0.3 	//	DBL_MIN 	2.0
#define PI_INTEGRAL_SCALE  			0.0 	//	0.0 		DBL_MAX

#define PI_PROPORTIONAL_CONST  		0.0 	//	0.0 		DBL_MAX
#define PI_PROPORTIONAL_EXPONENT 	-0.3 	//	-DBL_MAX 	DBL_MAX
#define PI_PROPORTIONAL_NORM_MAX 	0.7 	//	DBL_MIN 	1.0
#define PI_PROPORTIONAL_SCALE 		0.0 	//	0.0,		DBL_MAX




#endif