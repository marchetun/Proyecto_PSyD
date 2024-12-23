/*
 * Extra_Fuctions.h
 *
 *  Created on: 23/12/2024
 *      Author: march
 */

#ifndef EXTRA_FUCTIONS_H_
#define EXTRA_FUCTIONS_H_

#include <rtc.h>//rtc incluye common_types
char* calculate_weekday(uint8 week_day);//Pasa el dia de la semana a string
boolean dates_comparator(rtc_time_t actual_time, rtc_time_t time);//devuelve true si actual time mayor o igual que time

#endif /* EXTRA_FUCTIONS_H_ */
