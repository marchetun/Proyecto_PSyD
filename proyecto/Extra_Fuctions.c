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

    default: return NULL;
    }
}

boolean dates_comparator(rtc_time_t actual_time, rtc_time_t time) {
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

void reset_rtc_time(rtc_time_t* time) {
    time->sec = 0;
    time->min = 0;
    time->hour = 0;
    time->mday = 0;
    time->wday = 0;
    time->mon = 0;
    time->year = 0;
}

// Función para ajustar los límites de cada campo
void adjust_rtc_time(rtc_time_t* time) {
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


void apply_credits(rtc_time_t* time, uint16 credits) {
    if (time == NULL) {
        return;
    }

    // Convertir créditos a minutos totales
    uint16 total_minutes = time->min + credits;
    time->hour += total_minutes / 60;
    time->min = total_minutes % 60;

    // Ajustar todos los campos
    adjust_rtc_time(time);
}

void lcd_putint_time(uint16 x, uint16 y, uint16 color, uint8 num) {
    if (num < 10) {
        lcd_putchar(x, y, color, '0');
        lcd_putint(x + 10, y, color, num);
    }
    else {
        lcd_putint(x, y, color, num);
    }
}
