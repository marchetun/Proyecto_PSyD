/*
 * Extra_Fuctions.h
 *
 *  Created on: 23/12/2024
 *      Author: march
 */

#ifndef EXTRA_FUCTIONS_H_
#define EXTRA_FUCTIONS_H_

#include <rtc.h>//rtc incluye common_types

 //Pasa el dia de la semana a string
char* calculate_weekday(uint8 week_day);

//devuelve true si actual time mayor o igual que time
boolean dates_comparator(rtc_time_t actual_time, rtc_time_t time);

//Pone todos los par�metros del time a 0
void reset_rtc_time(rtc_time_t* time);

// Funci�n para ajustar los l�mites de cada campo
void adjust_rtc_time(rtc_time_t* time);

// Funci�n principal para aplicar los cr�ditos
void apply_credits(rtc_time_t* time, uint16 credits);

//Funcion para a�adir un 0 a la izquierda en fechas y horas
void lcd_putint_time(uint16 x, uint16 y, uint16 color, uint8 num);

#endif /* EXTRA_FUCTIONS_H_ */
