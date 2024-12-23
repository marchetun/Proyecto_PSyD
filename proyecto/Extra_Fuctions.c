/*
 * Extra_Fuctions.c
 *
 *  Created on: 23/12/2024
 *      Author: march
 */
#include <Extra_Fuctions.h>


char* calculate_weekday(uint8 week_day) {
	switch (week_day) {
	        case 1: return "dom"; // Domingo
	        case 2: return "lun"; // Lunes
	        case 3: return "mar"; // Martes
	        case 4: return "mie"; // Miércoles
	        case 5: return "jue"; // Jueves
	        case 6: return "vie"; // Viernes
	        case 7: return "sab"; // Sábado
    }
}
boolean dates_comparator(rtc_time_t actual_time, rtc_time_t time){
	    // Compara cada campo de mayor a menor granularidad
	    if (actual_time.year > time.year) return TRUE;
	    if (actual_time.year < time.year) return FALSE;

	    if (actual_time.mon > time.mon) return TRUE;
	    if (actual_time.mon < time.mon) return FALSE;

	    if (actual_time.mday > time.mday) return TRUE;
	    if (actual_time.mday < time.mday) return FALSE;

	    if (actual_time.hour > time.hour) return TRUE;
	    if (actual_time.hour < time.hour) return FALSE;

	    if (actual_time.min > time.min) return TRUE;
	    if (actual_time.min < time.min) return FALSE;

	    if (actual_time.sec >= time.sec) return TRUE;


	    return FALSE;
}
