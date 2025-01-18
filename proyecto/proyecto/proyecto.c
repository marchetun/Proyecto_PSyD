﻿#include <s3c44b0x.h>
#include <s3cev40.h>
#include <common_types.h>
#include <system.h>
#include <leds.h>
#include <segs.h>
#include <uart.h>
#include <rtc.h>
#include <timers.h>
#include <keypad.h>
#include <lcd.h>
#include <ts.h>
#include <rtc.h>
#include "Extra_Fuctions.h"
#include "kernelcoop.h"

/////////////////////////////////////////////////////////////////////////////
// CONFIGURACION
/////////////////////////////////////////////////////////////////////////////

#define MAXPLACES              (10)

/////////////////////////////////////////////////////////////////////////////
// Declaracion de tipos
/////////////////////////////////////////////////////////////////////////////

typedef struct
{
    rtc_time_t startTime;   // Hora en la que se ocupa la plaza
    rtc_time_t endTime;     // Hora en la que expira la plaza
    boolean occupied;   // Indica si la plaza esta ocupada
} plaza_t;

typedef plaza_t parking_t[MAXPLACES];

// Declaro esta estructura para anyadir estados restantes a la main_task()
typedef enum
{
    WELCOME_SCREEN,     // Estado inicial, muestra pantalla de bienvenida (nuevo demo_waiting)
    SELECT_PLACE,       // Estado de seleccion de plaza
    SHOW_TARIFF,        // Estado que muestra la tarifa
    PROCESS_PAYMENT,    // Estado que procesa el pago
    PRINT_TICKET       // Estado que imprime el ticket
} parkingState_t;

/////////////////////////////////////////////////////////////////////////////
// Declaracion de funciones
/////////////////////////////////////////////////////////////////////////////

void setup(void);

uint8 checkSelectedPlace(uint16 x, uint16 y); //Devuelve el indice de la plaza seleccionada segun coordenadas.

void drawParkingGrid(void);
void plotWelcomeScreen(void);
void showPlaceOccupiedMessage(uint8 placeNum);   	  // Muestra la pantalla con el mensaje de plaza ocupada.
void showTariffScreen(uint8 placeNum, uint16 credit);    // Muestra la pantalla con el mensaje de tarifa.

/////////////////////////////////////////////////////////////////////////////
// Declaracion de tareas
/////////////////////////////////////////////////////////////////////////////

void tsScanTask(void);
void coinsAcceptorTask(void);
void clockTask(void);
void coinsMoverTask(void);
void mainTask(void);
void ticketPrinterTask(void);

/////////////////////////////////////////////////////////////////////////////
// Declaracion de recursos
/////////////////////////////////////////////////////////////////////////////

parking_t parking;

rtc_time_t actual_time;

boolean free;//para saber si es dia gratuito

volatile uint8 selectedPlace;

struct mbox1 {                /* mailbox donde la coinsAcceptorTask indica a mainTask la moneda introducida */
    boolean flag;
    uint8   cents;
} coinAcceptorMsg;

struct mbox2 {                /* mailbox donde la tsScanTask indica a mainTask la posicion de la pantalla presionada */
    boolean flag;
    uint16  x, y;
} tsPressedMsg;

struct mbox3 {                 /* mailbox donde la mainTask indica a coinsMoverTask si debe aceptar o devolver las monedas introducidas */
    boolean flag;
    boolean accept;
} coinsMoverMsg;

boolean printTicketFlag;    /* mailbox donde la mainTask indica a ticketPrintTask que imprima un ticket */

/*******************************************************************/

void main(void)
{
    sys_init();
    leds_init();
    segs_init();
    uart0_init();
    rtc_init();
    timers_init();
    keypad_init();
    lcd_init();
    ts_init();

    lcd_on();
    ts_on();

    setup();

    scheduler_init();                       /* Inicializa el kernel */

    create_task(tsScanTask, 5);    /* Crea las tareas de la aplicacion... */
    create_task(coinsAcceptorTask, 5);    /* ... el kernel asigna la prioridad segun orden de creacion */
    create_task(mainTask, 10);    /* ... las tareas mas frecuentes tienen mayor prioridad (criterio Rate-Monotonic-Scheduling) */
    create_task(coinsMoverTask, 10);
    create_task(clockTask, 10);
    create_task(ticketPrinterTask, 10);

    timer0_open_tick(scheduler, TICKS_PER_SEC);  /* Instala scheduler como RTI del timer0  */

    while (1)
    {
        sleep();                /* Entra en estado IDLE, sale por interrupcion */
        dispacher();            /* Las tareas preparadas se ejecutan en esta hebra (background) en orden de prioridad */
    }
}

/*******************************************************************/

/*
** Tarea principal, se activa cada 100 ms muestreando los mensajes enviados de otras tareas y actuando en consecuencia
*/
void mainTask(void)
{
    static boolean init = TRUE;
    static parkingState_t state;
    static uint16 credit;
    static uint16 ticks;

    rtc_time_t endTime_aux;//tiempo auxiliar para aplicar creditos
    if (init)
    {
        init = FALSE;
        plotWelcomeScreen();
        credit = 0;
        state = WELCOME_SCREEN;
        free = FALSE;//para saber si el dia de la semana es gratis
        if(actual_time.wday==1)//Si es domingo, la tarifa es gratuita, en este caso lo ponemos al inicializar pero
        	free = TRUE;    // En una maquina real en la que el progama se ejectua durante días habría que comprobarlo en la WELCOME_SCREEN

    }
    else switch (state)
    {
    case WELCOME_SCREEN:                        /* Estado en donde rechaza monedas y espera la pulsacion de la pantalla */
        if (tsPressedMsg.flag)                    /* Chequea si se ha pulsado la pantalla (mensaje recibido de la tarea tsPressedTask) */
        {
            tsPressedMsg.flag = FALSE;                 /* Marca el mensaje como leido */
            lcd_clear();                               /* Borra pantalla */

            if(is_on_time(&actual_time)){//si esta dentro del horario pasa a seleccionar plaza
            	//Dibujo LCD
            	        lcd_puts_x2(32, 40, BLACK, "Seleccione plaza");
            	        drawParkingGrid();

            	   state = SELECT_PLACE;                  /* Salta al estado demo_acceptCoins ... */
            	            ticks = 500;                               /* ... en el que debera permanecer un maximo de 500 ticks sin no hay actividad */
            }else
            {
            	lcd_puts(84, 112, BLACK, "Fuera de Servicio");//Mostramos mensaje de fuera de servicio
            	sw_delay_ms(2000);//delay para que pueda leer
            	lcd_clear();//limpiamos
            	plotWelcomeScreen();//seguimos en welcome_screen y mostramos su pantalla
            }

        }
        if (coinAcceptorMsg.flag)                /* Chequea si se ha introducido una moneda (mensaje recibido de la tarea coinAcceptorTask) */
        {
            coinAcceptorMsg.flag = FALSE;              /* Marca el mensaje como leido */
            coinsMoverMsg.accept = FALSE;              /* Envia un mensaje para que la moneda se devuelva */
            coinsMoverMsg.flag = TRUE;
        }
        break;
    case SELECT_PLACE: /* Estado en donde rechaza monedas y espera la seleccion de la plaza */

        if (!(--ticks))                           /* Decrementa ticks y chequea si ha permanecido en este estado el tiempo maximo */
        {
        	lcd_clear();
            plotWelcomeScreen();                       /* Visualiza pantalla inicial */
            state = WELCOME_SCREEN;                      /* Salta al estado demo_waiting */
        }

        if (tsPressedMsg.flag)                    /* Chequea si se ha pulsado la pantalla (mensaje recibido de la tarea tsPressedTask) */
        {
            tsPressedMsg.flag = FALSE;                /* Marca el mensaje como leido */

            //Devuelve en num. de plaza pulsado segun las coordenadas
            selectedPlace = checkSelectedPlace(tsPressedMsg.x, tsPressedMsg.y);
            if (selectedPlace != 0) {
                if (!parking[selectedPlace - 1].occupied) {
                	lcd_clear();
                    showTariffScreen(selectedPlace, credit);
                    state = SHOW_TARIFF;
                    ticks = 500;
                }
                else {
                    showPlaceOccupiedMessage(selectedPlace);
                    state = SELECT_PLACE;
                    ticks = 500;
                }
            }
        }
        // Seguimos rechazando monedas durante la seleccion de plaza
        if (coinAcceptorMsg.flag) {
            coinAcceptorMsg.flag = FALSE;
            coinsMoverMsg.accept = FALSE;
            coinsMoverMsg.flag = TRUE;
        }

        break;
    case SHOW_TARIFF:  // Procesamos monedas introducidas
        if (!(--ticks)) // Timeout de inactividad (5 segundos)
        {
        	lcd_clear();
            plotWelcomeScreen();
            // Devolvemos las monedas introducidas
            if (credit > 0)
            {
                coinsMoverMsg.accept = FALSE;
                coinsMoverMsg.flag = TRUE;
                credit = 0;
            }
            credit = 0;
            state = WELCOME_SCREEN;
        }else{

        	 // Actualizar la hora fin para que siempre cuadre con la hora actual
        	             rtc_gettime(&endTime_aux);
        	            // Aplico los creditos al auxiliar
        	            apply_credits(&endTime_aux, credit);
        	            if(!is_on_time(&endTime_aux)){//Si ha excedido el límite devuelve las monedas y vuelve al estado anterior
        	            	 coinsMoverMsg.accept = FALSE;// no acepta monedas
        	            	                coinsMoverMsg.flag = TRUE;//levanta flag
        	            	                // Si nos pasamos del maximo, devolvemos todas las monedas
        	            	                credit = 0;
        	            	                coinAcceptorMsg.cents = 0;
        	            	                ticks = 500;  //Restauro ticks de SELECT_PLACE
        	            	                state = SELECT_PLACE;
        	            	                lcd_clear();
        	            	                                	 lcd_puts(84, 112, BLACK, " Fuera de Horario ");
        	            	                                	 sw_delay_ms(2000);
        	            	                                	 lcd_clear();
        	            	                                	                //Dibujo LCD
        	            	                                	                            	        lcd_puts_x2(32, 40, BLACK, "Seleccione plaza");
        	            	                                	                            	        drawParkingGrid();
        	            }else{
        	            	// Mostramos la hora
        	            	            show_date(100, 194, endTime_aux);
        	            }


        }



        // Procesamos monedas introducidas
        if (coinAcceptorMsg.flag)
        {
            ticks = 50;   /* Restaura el tiempo maximo sin actividad que permanece en este estado */
            coinAcceptorMsg.flag = FALSE;	/* Marca el mensaje como leido */



            // Verificamos que no nos pasemos del maximo (240 centimos)
            if (credit + coinAcceptorMsg.cents <= 240 )
            {
               if(free)//acepta monedas, no acepta si es gratis
            	   coinsMoverMsg.accept = FALSE;
               else
            	   coinsMoverMsg.accept = TRUE;

                coinsMoverMsg.flag = TRUE;//levanta flag
                credit += coinAcceptorMsg.cents;
                coinAcceptorMsg.cents = 0;

                showTariffScreen(selectedPlace,  credit);//mostramos pantalla de nuevo
                ticks = 500;   /* Restaura el tiempo maximo sin actividad que permanece en este estado */
            }
            else
            {
                coinsMoverMsg.accept = FALSE;// no acepta monedas
                coinsMoverMsg.flag = TRUE;//levanta flag
                // Si nos pasamos del maximo, devolvemos todas las monedas
                credit = 0;
                coinAcceptorMsg.cents = 0;
                ticks = 500;  //Restauro ticks de SELECT_PLACE
                state = SELECT_PLACE;
                if(!is_on_time(&endTime_aux)){
                	 lcd_clear();
                	 lcd_puts(84, 112, BLACK, " Fuera de Horario ");
                	 sw_delay_ms(2000);
                }
                lcd_clear();
                //Dibujo LCD
                            	        lcd_puts_x2(32, 40, BLACK, "Seleccione plaza");
                            	        drawParkingGrid();
            }
        }

        // Si se pulsa la pantalla, verificamos el credito
        if (tsPressedMsg.flag)
        {
            tsPressedMsg.flag = FALSE;
            state = PROCESS_PAYMENT;
        }
        break;
    case PROCESS_PAYMENT: // Estado donde se valora el credito introducido una vez se pulsa la pantalla
        if (credit < 20) // Credito insuficiente
        {
            lcd_clear();
            lcd_puts(84, 112, BLACK, "Saldo insuficiente");
            credit = 0;
            sw_delay_ms(2000);
            state = SHOW_TARIFF;
            showTariffScreen(selectedPlace, credit);
            ticks = 50;
            ticks = 500;
        }
        else // Credito valido, procesamos el pago
        {
            lcd_clear();
            lcd_puts(84, 112, BLACK, "Procesando pago...");
            // Aceptamos las monedas antes de procesar el pago
            coinsMoverMsg.accept = TRUE;	 /* Envia un mensaje para que las monedas se acepten */
            coinsMoverMsg.flag = TRUE;

            // Obtenemos la hora actual, aunque la estamos obteniendo cada segundo de reloj
            rtc_gettime(&actual_time);

            // Actualizamos la informacion de la plaza
            parking[selectedPlace - 1].occupied = TRUE;
            parking[selectedPlace - 1].startTime = actual_time;  // Tiempo inicial en minutos


            parking[selectedPlace - 1].endTime = parking[selectedPlace - 1].startTime;
            // Aplico los creditos
            apply_credits(&parking[selectedPlace - 1].endTime, credit);
            // Activamos la impresion del ticket
            printTicketFlag = TRUE;
            state = PRINT_TICKET;

            sw_delay_ms(2000);//una vez finalizado pago, delay de lectura
        }
        break;
    case PRINT_TICKET:

        // Esperamos a que se complete la impresion
        if (!printTicketFlag)
        {
            plotWelcomeScreen();
            credit = 0;
            state = WELCOME_SCREEN;
        }
        break;
    }
}

/*
** Emula el comportamiento de la impresora de tickets:
**   Cada segundo muestrea si ha recibido un mensaje de la tarea principal enviar a traves de la UART el texto del ticket del aparcamiento elegido
*/
void ticketPrinterTask(void)//Revisado
{
    if (printTicketFlag) {
        printTicketFlag = FALSE;//bajamos flag
        // Imprimimos una linea de guiones como separador
        uart0_puts("--------------------------------\n");

        // Primera linea: numero de plaza
        uart0_puts("Plaza ");
        uart0_putint(selectedPlace);
        uart0_puts("\n");

        // Segunda linea: "Fin de estacionamiento:"
        uart0_puts("Fin de estacionamiento:\n");

        uart0_puts(calculate_weekday(parking[selectedPlace - 1].endTime.wday));
        uart0_putchar(',');
        uart0_putchar(' ');
        if (parking[selectedPlace - 1].endTime.mday < 10) uart0_putint(0);
        uart0_putint(parking[selectedPlace - 1].endTime.mday);
        uart0_putchar('/');
        if (parking[selectedPlace - 1].endTime.mon < 10) uart0_putint(0);
        uart0_putint(parking[selectedPlace - 1].endTime.mon);
        uart0_putchar('/');
        if (parking[selectedPlace - 1].endTime.year < 10) uart0_putint(0);
        uart0_putint(parking[selectedPlace - 1].endTime.year);
        uart0_putchar(' ');
        if (parking[selectedPlace - 1].endTime.hour < 10) uart0_putint(0);
        uart0_putint(parking[selectedPlace - 1].endTime.hour);
        uart0_putchar(':');
        if (parking[selectedPlace - 1].endTime.min < 10) uart0_putint(0);
        uart0_putint(parking[selectedPlace - 1].endTime.min);
        uart0_putchar(':');
        if (parking[selectedPlace - 1].endTime.sec < 10) uart0_putint(0);
        uart0_putint(parking[selectedPlace - 1].endTime.sec);
        uart0_puts("\n");

        // Linea separadora final
        uart0_puts("--------------------------------\n");

    }
}

/*
** Cada segundo visualiza la fecha/hora en la pantalla y libera aquellas plazas cuya hora de finalizacion haya pasado
*/
void clockTask(void)//Revisado
{
    uint8 i;
    //Saco la hora
    rtc_gettime(&actual_time);


    // Mostramos la hora
    show_date(60, 8, actual_time);

    // Liberar plazas de parking cuya hora de finalizacion haya pasado

    for (i = 0; i < MAXPLACES; i++) {
        if (parking[i].occupied) {
            if (dates_comparator(actual_time, parking[i].endTime)) {
                parking[i].occupied = FALSE;  // Marca como libre
                reset_rtc_time(&parking[i].startTime);//Reseteamos inicio y final de plaza
                reset_rtc_time(&parking[i].endTime);
            }
        }
    }
}

/*
** Cada 50 ms muestrea la touchscreen y envia un mensaje a la tarea principal con la posicion del lugar pulsado
*/
void tsScanTask(void)//Revisado
{
    static boolean init = TRUE;
    static enum { wait_keydown, scan, wait_keyup } state;
    uint16 x, y;
    if (init) {
        init = FALSE;
        state = wait_keydown;
    }
    else switch (state) {
    case wait_keydown:                        /* Estado esperando la presion de la pantalla */
        if (ts_pressed())                        /* Chequea la pantalla esta presionada */
            state = scan;                         /* Salta al estado scan */
        break;
    case scan:
        ts_getpos(&x, &y);   //Aqui tenia que recibir punteros      /* Estado en que escanea la pantalla */
        tsPressedMsg.x = x;             /* Obtiene la posicion X */
        tsPressedMsg.y = y;             /* Obtiene la posicion Y */
        tsPressedMsg.flag = TRUE;                /* Marca el mensaje como valido */
        state = wait_keyup;                      /* Cambia al estado wait_keyup */
        break;
    case wait_keyup:                           /* Estado esperando que se deje de presionar la pantalla */
        if (!ts_pressed())                     /* Chequea que la pantalla ya no esta presionada */
            state = wait_keydown;                /* Vuelve al estado inicial */
        break;
    }
}

/*
** Emula el comportamiento de un reconocedor de monedas:
**   Cada 50 ms muestrea el keypad y envia un mensaje a la tarea principal con el valor de la moneda asociada a la tecla
*/
void coinsAcceptorTask(void) {//Revisado
    static boolean init = TRUE;
    static enum { wait_keydown, scan, wait_keyup } state;

    if (init) {
        init = FALSE;
        state = wait_keydown;
    }
    else switch (state) {
    case wait_keydown:                        /* Estado esperando la presion teclas */
        if (keypad_pressed())                    /* Chequea si hay una tecla presionada */
            state = scan;                         /* Salta al estado scan */
        break;
    case scan:                                /* Estado en que escanea el teclado */
        switch (keypad_scan())                   /* Lee el teclado */
        {
        case KEYPAD_KEYF:                         /* La tecla F esta asociada a la moneda de 0,10 euros */
            coinAcceptorMsg.cents = 10;           /* Envia un mensaje con el valor de la moneda */
            coinAcceptorMsg.flag = TRUE;
            break;
        case KEYPAD_KEYE:                         /* La tecla E esta asociada a la moneda de 0,20 euros */
            coinAcceptorMsg.cents = 20;           /* Envia un mensaje con el valor de la moneda */
            coinAcceptorMsg.flag = TRUE;
            break;
        case KEYPAD_KEYD:                         /* La tecla D esta asociada a la moneda de 0,50 euros */
            coinAcceptorMsg.cents = 50;           /* Envia un mensaje con el valor de la moneda */
            coinAcceptorMsg.flag = TRUE;
            break;
        case KEYPAD_KEYC:                         /* La tecla C esta asociada a la moneda de 1,00 euros */
            coinAcceptorMsg.cents = 100;           /* Envia un mensaje con el valor de la moneda */
            coinAcceptorMsg.flag = TRUE;
            break;
        }
        state = wait_keyup;                       /* Salta al estado wait_keyup */
        break;
    case wait_keyup:                          /* Estado esperando la depresion de teclas  */
        if (!keypad_pressed())                   /* Chequea si la tecla ya no esta presionada */
            state = wait_keydown;                     /* Salta al estado wait_keydown */
        break;
    }
}

/*
** Emula el comportamiento del dispositivo que envia las monedas a la alcancia o al cajetin de devolucion:
**   Cada 100 ms muestrea si ha recibido un mensaje de la tarea principal para mover monedas
**   Si van a la alcancia activa durante 1 s los leds y muestra una A en los segs
**   Si van al cajetin de devolucion activa durante 1 segundo los leds y muestra una D en los segs
*/
void coinsMoverTask(void)// Revisado
{
    static boolean init = TRUE;
    static enum { off, on } state;
    static uint32 ticks;

    if (init)
    {
        init = FALSE;
        state = off;
    }
    else switch (state)
    {
    case off:                                /* Estado con leds y 7-segs apagados */
        if (coinsMoverMsg.flag)                /* Chequea si ha recibido mensaje */
        {
            coinsMoverMsg.flag = FALSE;            /* Marca el mensaje como leido */
            if (coinsMoverMsg.accept)             /* Muestra el mensaje que corresponda en segs */
                segs_putchar(0xA);
            else
                segs_putchar(0xD);
            led_on(LEFT_LED);                    /* Enciende leds */
            led_on(RIGHT_LED);
            state = on;                            /* Salta al estado on ... */
            ticks = 10;                            /* ... en el que debera permanecer 10 ticks */
        }
        break;
    case on:                                /* Estado con leds y 7-segs encendidos */
        if (!(--ticks))                        /* Decrementa ticks y chequea si ha permanecido en este estado el tiempo requerido */
        {
            segs_off();                            /* Apaga segs */
            led_off(LEFT_LED);                   /* Apaga leds */
            led_off(RIGHT_LED);
            state = off;                           /* Salta al estado off */
        }
        break;
    }
}

/*******************************************************************/

void setup(void)//Revisado
{
    // Inicializacion de variables o configuraciones necesarias
    int i;
    for (i = 0; i < MAXPLACES; i++) {
        parking[i].occupied = FALSE;
    }
    coinAcceptorMsg.flag = FALSE;
    coinAcceptorMsg.cents = 0;

    tsPressedMsg.flag = FALSE;
    tsPressedMsg.x = 0;
    tsPressedMsg.y = 0;

    coinsMoverMsg.flag = FALSE;
    coinsMoverMsg.accept = FALSE;
    printTicketFlag = FALSE;

    //Iniciamos la hora del sistema

    //Iniciamos la hora del sistema (BUCLES POR SI SE INTRODUCE FECHA ERRONEA)
    uart0_puts("\nIntroduzca nueva fecha\n");
    do {
        uart0_puts("  - Dia: ");
        actual_time.mday = (uint8)uart0_getint();
    } while (actual_time.mday < 1 || actual_time.mday > 31);

    do {
        uart0_puts("  - Dia de la semana (Del 1 al 7, 1 es domingo): ");
        actual_time.wday = (uint8)uart0_getint();
    } while (actual_time.wday < 1 || actual_time.wday > 7);

    do {
        uart0_puts("  - Mes: ");
        actual_time.mon = (uint8)uart0_getint();
    } while (actual_time.mon < 1 || actual_time.mon > 12);

    do {
        uart0_puts("  - Año (2 digitos): ");
        actual_time.year = (uint8)uart0_getint();
    } while (actual_time.year < 0 || actual_time.year > 99);


    uart0_puts("Introduzca nueva hora\n");
    do {
        uart0_puts("  - Hora: ");
        actual_time.hour = (uint8)uart0_getint();
    } while (actual_time.hour < 0 || actual_time.hour > 23);

    do {
        uart0_puts("  - Minuto: ");
        actual_time.min = (uint8)uart0_getint();
    } while (actual_time.min < 0 || actual_time.min > 60);

    do {
        uart0_puts("  - Segundo: ");
        actual_time.sec = (uint8)uart0_getint();
    } while (actual_time.sec < 0 || actual_time.sec > 60);



    rtc_puttime(&actual_time);
}

/*******************************************************************/

void plotWelcomeScreen(void) {//Revisado, alomejor cuadrar posiciones de cadenas en el lcd
    lcd_clear();
    rtc_gettime(&actual_time);


    // Mensaje principal centrado
    lcd_puts_x2(24, 40, BLACK, "Pulse la pantalla");
    lcd_puts_x2(56, 80, BLACK, "para comenzar");

    // Dibujamos el rectangulo para el horario
    lcd_draw_box(1, 128, 319, 218, BLACK, 1);

    // Titulo del horario centrado
    lcd_puts(70, 120, BLACK, "HORARIO DE FUNCIONAMIENTO");

    // Horarios
    lcd_puts(24, 140, BLACK, "dom: gratis        jue: 09:00-21:00");
    lcd_puts(24, 160, BLACK, "lun: 09:00-21:00   vie: 09:00-21:00");
    lcd_puts(24, 180, BLACK, "mar: 09:00-21:00   sab: 09:00-15:00");
    lcd_puts(24, 200, BLACK, "mie: 09:00-21:00");
}

//Funcion que pinta la seleccion de plazas displonibles.
void drawParkingGrid(void) {//Revisado, alomejor cuadrar posiciones de cadenas en el lcd
    uint16 x = ((320 / (MAXPLACES / 2)) / 2) - 8, y = 130, i;//posiciones de inicio de numero

    //Bucle para comprobar estado de la plaza

    for (i = 1; i <= MAXPLACES / 2; i++) {
        lcd_draw_box((i - 1) * (320 / (MAXPLACES / 2)), 113, i * (320 / (MAXPLACES / 2)) - 1, 173, BLACK, 1);
        // Pinta box
        if (parking[i - 1].occupied)
            lcd_putchar_x2(x, y, BLACK, 'X');
        else {
            if (i >= 10)//Si son dos digitos cuadramos
                x = x - 8;
            lcd_putint_x2(x, y, BLACK, i);
        }


        x = x + (320 / (MAXPLACES / 2));
    }
    x = ((320 / (MAXPLACES / 2)) / 2) - 8;
    y = 190;
    for (; i <= MAXPLACES; i++) {
        lcd_draw_box((i - 1) * (320 / (MAXPLACES / 2)), 173, i * (320 / (MAXPLACES / 2)) - 1, 233, BLACK, 1);
        if (parking[i - 1].occupied)
            lcd_putchar_x2(x, y, BLACK, 'X');
        else {
            if (i >= 10)//Si son dos digitos cuadramos
                x = x - 8;
            lcd_putint_x2(x, y, BLACK, i);
        }

        x = x + (320 / (MAXPLACES / 2));
    }
}
uint8 checkSelectedPlace(uint16 x, uint16 y) { // Para dos filas
    int col = x / (320 / (MAXPLACES / 2));
    int row;

    if (y >= 119 && y < 179) {
        row = 0;
    }
    else if (y >= 179 && y < 239) {
        row = 1;
    }
    else {
        return 0; // Fuera de los límites verticales
    }

    // Verificamos que el toque esté dentro de los límites horizontales
    if (col >= 0 && col < (MAXPLACES / 2)) {
        return (row * (MAXPLACES / 2)) + col + 1;
    }

    return 0; // Retorna 0 si no se seleccionó ninguna plaza válida
}

void showPlaceOccupiedMessage(uint8 placeNum) {
    lcd_clear();

    // Primera linea - Plaza X ocupada (en grande)
    lcd_puts_x2(32, 60, BLACK, "Plaza ");
    lcd_putint_x2(120, 60, BLACK, placeNum);
    lcd_puts_x2(160, 60, BLACK, "ocupada");

    // Segunda linea - Fin de estacionamiento
    lcd_puts(40, 120, BLACK, "Fin: ");
    show_date(80, 120, parking[placeNum - 1].endTime);

    sw_delay_ms(2000);
    lcd_clear();
    //Dibujo LCD
                	        lcd_puts_x2(32, 40, BLACK, "Seleccione plaza");
                	        drawParkingGrid();
}

void showTariffScreen(uint8 placeNum, uint16 credit)
{
    // Dibujamos el rectangulo para la tarifa
    lcd_draw_box(1, 40, 319, 130, BLACK, 1);
if(free){
	// Titulo centrado
		    lcd_puts(128, 32, BLACK, "TARIFA GRATUITA");

		    lcd_puts(32, 56, BLACK, "Precio por minuto: 0,00 euros");

		    //Credito
		    	           lcd_puts(88, 178, BLACK, "Minutos: ");
		    	           lcd_putint(154, 178, BLACK, credit );

		    	           lcd_puts(36, 210, BLACK, "Seleccione la cantidad de minutos ");
}
else{
	// Titulo centrado
	    lcd_puts(128, 32, BLACK, "TARIFA");
	    // Informacion de tarifa dentro del rectangulo
	        lcd_puts(32, 56, BLACK, "Precio por minuto: 0,01 euros");

	        //Credito
	           lcd_puts(88, 178, BLACK, "Credito: ");
	           lcd_putint(154, 178, BLACK, credit / 100);
	           lcd_putchar(162, 178, BLACK, ',');
	           lcd_putint(178, 178, BLACK, credit % 100);
	           lcd_puts(198, 178, BLACK, " euros");

	           lcd_puts(106, 210, BLACK, "Inserte monedas");

}
//Comunes
lcd_puts(32, 80, BLACK, "Estancia minima:  20 minutos");
lcd_puts(32, 104, BLACK, "Estancia maxima: 240 minutos");






    // Informacion de plaza
    lcd_puts_x2(106, 142, BLACK, "Plaza ");
    lcd_putint_x2(194, 142, BLACK, placeNum);




    //Fecha Fin
    lcd_puts(50, 194, BLACK, " Fin: ");


    // Mensajes finales

    lcd_puts(48, 222, BLACK, "Pulse la pantalla para aceptar");



}
