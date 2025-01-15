/*
 * Extra_Fuctions.c
 *
 *  Created on: 23/12/2024
 *      Author: march
 */
#include "Extra_Fuctions.h"


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
void reset_rtc_time(rtc_time_t *time) {
    time->sec = 0;
    time->min = 0;
    time->hour = 0;
    time->mday = 0;
    time->wday = 0;
    time->mon = 0;
    time->year = 0;
}
// Función para ajustar los límites de cada campo
void adjust_rtc_time(rtc_time_t *time) {
    static const uint8 days_in_month[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

    if (time->sec >= 60) {
        time->min += time->sec / 60;
        time->sec %= 60;
    }
    if (time->min >= 60) {
        time->hour += time->min / 60;
        time->min %= 60;
    }
    if (time->hour >= 24) {
        time->mday += time->hour / 24;
        time->hour %= 24;
    }

    // Ajustar días y meses
    uint8 max_days = days_in_month[time->mon - 1];
    if (time->mon == 2 && ((time->year % 4 == 0 && time->year % 100 != 0) || time->year % 400 == 0)) {
        max_days = 29; // Año bisiesto
    }
    if (time->mday > max_days) {
        time->mon += time->mday / max_days;
        time->mday = (time->mday - 1) % max_days + 1;
    }
    if (time->mon > 12) {
        time->year += time->mon / 12;
        time->mon = (time->mon - 1) % 12 + 1;
    }

    // Ajustar día de la semana
    time->wday = ((time->wday - 1 + time->mday) % 7) + 1;
}


void apply_credits(rtc_time_t *time, uint8 credits) {
	    // Si el total supera el rango representable, ajustar primero los créditos
	    uint16 total_minutes = time->min + credits;
	    if (total_minutes > 255) {
	        uint16 extra_hours = total_minutes / 60; // Horas completas adicionales
	        time->hour += extra_hours;
	        time->min = total_minutes % 60; // Restante de minutos
	    } else {
	        time->min = total_minutes;
	    }
    adjust_rtc_time(time); // Ajustar los campos de la estructura
}

void show_date(int x, int y, rtc_time_t actual_time){
	lcd_puts(x, y, BLACK, calculate_weekday(actual_time.wday));
	    lcd_putchar(x+24, y, BLACK, ',');
	    lcd_putint(x+32, y, BLACK, actual_time.mday);
	    lcd_putchar(x+48, y, BLACK, '/');
	    lcd_putint(x+56, y, BLACK, actual_time.mon);
	    lcd_putchar(x+72, y, BLACK, '/');
	    lcd_putint(x+80, y, BLACK, actual_time.year);

	    lcd_putint(x+116, y, BLACK, actual_time.hour);
	    lcd_putchar(x+132, y, BLACK, ':');
	    lcd_putint(x+140, y, BLACK, actual_time.min);
	    lcd_putchar(x+156, y, BLACK, ':');
	    lcd_putint(x+164, y, BLACK, actual_time.sec);
}
