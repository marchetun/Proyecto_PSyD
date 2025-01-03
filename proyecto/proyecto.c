#include <s3c44b0x.h>
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

#define MAXPLACES              (8)

/////////////////////////////////////////////////////////////////////////////
// Declaracion de tipos
/////////////////////////////////////////////////////////////////////////////

typedef struct
{
    rtc_time_t startTime;   // Hora en la que se ocup� la plaza
    rtc_time_t endTime;     // Hora en la que expir� la plaza
    boolean occupied;   // Indica si la plaza est� ocupada
} plaza_t;

typedef plaza_t parking_t[MAXPLACES];

// Declaro esta estructura para a�adir estados restantes a la main_task()
typedef enum
{
    WELCOME_SCREEN,     // Estado inicial, muestra pantalla de bienvenida (nuevo demo_waiting)
    SELECT_PLACE,       // Estado de selecci�n de plaza
    SHOW_TARIFF,        // Estado que muestra la tarifa
    PROCESS_PAYMENT,    // Estado que procesa el pago
    PRINT_TICKET       // Estado que imprime el ticket
} parkingState_t;

/////////////////////////////////////////////////////////////////////////////
// Declaracion de funciones
/////////////////////////////////////////////////////////////////////////////

void setup(void);

uint8 checkSelectedPlace(uint16 x, uint16 y); //Devuelve el �ndice de la plaza seleccionada seg�n coordenadas.

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

    if (init)
    {
        init = FALSE;
        plotWelcomeScreen();
        credit = 0;
        state = WELCOME_SCREEN;
    }
    else switch (state)
    {
    case WELCOME_SCREEN:                        /* Estado en donde rechaza monedas y espera la pulsacion de la pantalla */
        if (tsPressedMsg.flag)                    /* Chequea si se ha pulsado la pantalla (mensaje recibido de la tarea tsPressedTask) */
        {
            tsPressedMsg.flag = FALSE;                 /* Marca el mensaje como leido */
            lcd_clear();                               /* Borra pantalla */

            //Preparo cambio de estado a SELECT_PLACE
            lcd_puts_x2(24, 24, BLACK, "Seleccione plaza");
            drawParkingGrid();

            state = SELECT_PLACE;                  /* Salta al estado demo_acceptCoins ... */
            ticks = 500;                               /* ... en el que debera permanecer un maximo de 500 ticks sin no hay actividad */
        }
        if (coinAcceptorMsg.flag)                /* Chequea si se ha introducido una moneda (mensaje recibido de la tarea coinAcceptorTask) */
        {
            coinAcceptorMsg.flag = FALSE;              /* Marca el mensaje como leido */
            coinsMoverMsg.accept = FALSE;              /* Envia un mensaje para que la moneda se devuelva */
            coinsMoverMsg.flag = TRUE;
        }
        break;
    case SELECT_PLACE:                   			/* Estado en donde rechaza monedas y espera la selecci�n de la plaza */
        if (!(--ticks))                           /* Decrementa ticks y chequea si ha permanecido en este estado el tiempo maximo */
        {
            plotWelcomeScreen();                       /* Visualiza pantalla inicial */
            state = WELCOME_SCREEN;                      /* Salta al estado demo_waiting */
        }
        if (tsPressedMsg.flag)                    /* Chequea si se ha pulsado la pantalla (mensaje recibido de la tarea tsPressedTask) */
        {
            tsPressedMsg.flag = FALSE;                /* Marca el mensaje como leido */

            //Devuelve en n� de plaza pulsado seg�n las coordenadas
            selectedPlace = checkSelectedPlace(tsPressedMsg.x, tsPressedMsg.y);
            if (selectedPlace != 0) {
                if (!parking[selectedPlace - 1].occupied) {
                    showTariffScreen(selectedPlace, credit);
                    state = SHOW_TARIFF;
                    ticks = 50;
                }
                else {
                    showPlaceOccupiedMessage(selectedPlace);
                    state = SELECT_PLACE;
                    ticks = 500;
                }
            }
        }
        // Seguimos rechazando monedas durante la selecci�n de plaza
        if (coinAcceptorMsg.flag) {
            coinAcceptorMsg.flag = FALSE;
            coinsMoverMsg.accept = FALSE;
            coinsMoverMsg.flag = TRUE;
        }
        break;
    case SHOW_TARIFF:  // Procesamos monedas introducidas
        if (!(--ticks)) // Timeout de inactividad (5 segundos)
        {
            plotWelcomeScreen();
            // Devolvemos las monedas introducidas
            if (credit > 0)
            {
                coinsMoverMsg.flag = TRUE;
                credit = 0;
            }
            state = WELCOME_SCREEN;
        }

        // Procesamos monedas introducidas
        if (coinAcceptorMsg.flag)
        {
        	ticks = 50;   /* Restaura el tiempo maximo sin actividad que permanece en este estado */
            coinAcceptorMsg.flag = FALSE;	/* Marca el mensaje como leido */


            // Verificamos que no nos pasemos del m�ximo (240 c�ntimos)
            if (credit + coinAcceptorMsg.cents <= 240)
            {
            	coinsMoverMsg.accept = TRUE;//acepta monedas
            	coinsMoverMsg.flag = TRUE;//levanta flag
                credit += coinAcceptorMsg.cents;
                coinAcceptorMsg.cents = 0;
                // Actualizar toda la pantalla de tarifa con el nuevo cr�dito
                showTariffScreen(selectedPlace, credit);

            }
            else
            {
            	coinsMoverMsg.accept = FALSE;// no acepta monedas
            	coinsMoverMsg.flag = TRUE;//levanta flag
                credit = 0;
                coinAcceptorMsg.cents = 0;
                ticks = 500;  //Restauro ticks se SELECT_PLACE
                lcd_clear();
                //Preparo cambio de estado a SELECT_PLACE
                lcd_puts_x2(24, 24, BLACK, "Seleccione plaza");
                drawParkingGrid();
                state = SELECT_PLACE;
            }
        }

        // Si se pulsa la pantalla, verificamos el cr�dito
        if (tsPressedMsg.flag)
        {
            tsPressedMsg.flag = FALSE;
            state = PROCESS_PAYMENT;
        }
        break;
    case PROCESS_PAYMENT: // Estado donde se valora el credito introducido una vez se pulsa la pantalla
        if (credit < 20) // Cr�dito insuficiente
        {
            lcd_clear();
            lcd_puts(24, 48, BLACK, "Saldo insuficiente");
            credit = 0;
            sw_delay_ms(2000);
            state = SHOW_TARIFF;
            showTariffScreen(selectedPlace, credit);
            ticks = 50;
        }
        else // Cr�dito v�lido, procesamos el pago
        {
        	lcd_clear();
        	lcd_puts(24, 48, BLACK, "Procesando pago...");

            // Obtenemos la hora actual, aunque la estamos obteniendo cada segundo de reloj
            rtc_gettime(&actual_time);

            // Actualizamos la informaci�n de la plaza
            parking[selectedPlace - 1].occupied = TRUE;
            parking[selectedPlace - 1].startTime = actual_time;  // Tiempo inicial en minutos

            //Calculo el timpo final
            rtc_time_t endTime = parking[selectedPlace - 1].startTime;
            parking[selectedPlace - 1].endTime = parking[selectedPlace - 1].startTime;
            // Aplico los creditos
            apply_credits(&parking[selectedPlace - 1].endTime, credit);


            // Activamos la impresi�n del ticket
                   printTicketFlag = TRUE;
            state = PRINT_TICKET;

            sw_delay_ms(2000);//una vez finalizado pago, delay de lectura
        }
        break;
    case PRINT_TICKET:


        // Esperamos a que se complete la impresi�n
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
        // Imprimimos una l�nea de guiones como separador
        uart0_puts("--------------------------------\n");

        // Primera l�nea: n�mero de plaza
        uart0_puts("Plaza ");
        uart0_putint(selectedPlace);
        uart0_puts("\n");

        // Segunda l�nea: "Fin de estacionamiento:"
        uart0_puts("Fin de estacionamiento:\n");

        // Tercera l�nea: Fecha de fin, por ahora he dejado los segundos tambien ya que al comparar
        // los estoy teniendo en cuenta, habr�a que cambiarlo ya que en la demo no tiene en cuenta los segundos
        uart0_puts(calculate_weekday(parking[selectedPlace - 1].endTime.wday) + ',' + parking[selectedPlace - 1].endTime.mday + '/' + parking[selectedPlace - 1].endTime.mon + '/' + parking[selectedPlace - 1].endTime.year + ' ' +
            parking[selectedPlace - 1].endTime.hour + ':' + parking[selectedPlace - 1].endTime.min + ':' + parking[selectedPlace - 1].endTime.sec);
        uart0_puts("\n");

        // L�nea separadora final
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
    lcd_puts(24, 8, BLACK, calculate_weekday(actual_time.wday) + ',' + actual_time.mday + '/' + actual_time.mon + '/' + actual_time.year + ' ' +
        actual_time.hour + ':' + actual_time.min + ':' + actual_time.sec);

    // Liberar plazas de parking cuya hora de finalizaci�n haya pasado

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
    // Inicializaci�n de variables o configuraciones necesarias
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

    uart0_puts("\nIntroduzca nueva fecha\n");
    uart0_puts("  - Dia: ");
    actual_time.mday = (uint8)uart0_getint();
    uart0_puts("  - Dia de la semana (Del 1 al 7, 1 es domingo): ");
    actual_time.wday = (uint8)uart0_getint();
    uart0_puts("  - Mes: ");
    actual_time.mon = (uint8)uart0_getint();
    uart0_puts("  - A�o (2 digitos): ");
    actual_time.year = (uint8)uart0_getint();
    uart0_puts("Introduzca nueva hora\n");
    uart0_puts("  - Hora: ");
    actual_time.hour = (uint8)uart0_getint();
    uart0_puts("  - Minuto: ");
    actual_time.min = (uint8)uart0_getint();
    uart0_puts("  - Segundo: ");
    actual_time.sec = (uint8)uart0_getint();

    rtc_puttime(&actual_time);
}

/*******************************************************************/

void plotWelcomeScreen(void) {//Revisado, alomejor cuadrar posiciones de cadenas en el lcd
    lcd_clear();
    rtc_gettime(&actual_time);

    //Dibujamos hora del sistema
    lcd_puts(40, 8, BLACK, calculate_weekday(actual_time.wday) + ',' + actual_time.mday + '/' + actual_time.mon + '/' + actual_time.year + ' ' +
        actual_time.hour + ':' + actual_time.min + ':' + actual_time.sec);

    // Mensaje principal centrado
    lcd_puts_x2(32, 28, BLACK, "Pulse la pantalla");
    lcd_puts_x2(62, 64, BLACK, "para comenzar");

    // Dibujamos el rect�ngulo para el horario
    lcd_draw_box(0, 128, 319, 224, BLACK, 1);

    // T�tulo del horario centrado
    lcd_puts(70, 120, BLACK, "HORARIO DE FUNCIONAMIENTO");

    // Horarios
    lcd_puts(24, 140, BLACK, "dom: gratis        jue: 09:00-21:00");
    lcd_puts(24, 160, BLACK, "lun: 09:00-21:00   vie: 09:00-21:00");
    lcd_puts(24, 180, BLACK, "mar: 09:00-21:00   sab: 09:00-15:00");
    lcd_puts(24, 200, BLACK, "mie: 09:00-21:00");
}

//Funci�n que pinta la selecci�n de plazas displonibles.
void drawParkingGrid(void) {//Revisado, alomejor cuadrar posiciones de cadenas en el lcd
    uint16 x = 32, y = 136, i;
    /* Pinta cuadricula */
    lcd_draw_box(0, 119, 79, 179, BLACK, 1);
    lcd_draw_box(79, 119, 159, 179, BLACK, 1);
    lcd_draw_box(159, 119, 239, 179, BLACK, 1);
    lcd_draw_box(239, 119, 319, 179, BLACK, 1);
    lcd_draw_box(0, 179, 79, 239, BLACK, 1);
    lcd_draw_box(79, 179, 159, 239, BLACK, 1);
    lcd_draw_box(159, 179, 239, 239, BLACK, 1);
    lcd_draw_box(239, 179, 319, 239, BLACK, 1);

    /* Rotula cuadricula */
    //Bucle para comprobar estado de la plaza

    for (i = 1; i <= MAXPLACES / 2; i++) {
        if (parking[i-1].occupied)
            lcd_putchar_x2(x, y, BLACK, 'X');
        else
            lcd_putchar_x2(x, y, BLACK, '0' + i);

        x = x + 80;
    }
    x= 32;
    y = 196;
    for (; i <= MAXPLACES; i++) {
        if (parking[i-1].occupied)
            lcd_putchar_x2(x, y, BLACK, 'X');
        else
            lcd_putchar_x2(x, y, BLACK, '0' + i);
        x = x + 80;
    }
}

uint8 checkSelectedPlace(uint16 x, uint16 y) {//Revisado
    int col = x / 80;
    int row = (y - 119) / 60;

    // Verificamos que el toque est� dentro de los l�mites
    if (col >= 0 && col < 4 && row >= 0 && row < 2)
    {
        return (row * 4) + col + 1;
    }

    return 0; // Retorna 0 si no se seleccion� ninguna plaza v�lida
}

void showPlaceOccupiedMessage(uint8 placeNum) {
    lcd_clear();

    // Primera l�nea - Plaza X ocupada (en grande)
    lcd_puts_x2(70, 60, BLACK, "Plaza ");
    lcd_putint_x2(140, 60, BLACK, placeNum);
    lcd_puts_x2(160, 60, BLACK, " ocupada");

    // Segunda l�nea - Fin de estacionamiento
    lcd_puts(50, 120, BLACK, "Fin: ");
    lcd_puts(80, 120, BLACK, calculate_weekday(parking[placeNum - 1].endTime.wday));
    lcd_putchar(110, 120, BLACK, ',');
    lcd_putint(120, 120, BLACK, parking[placeNum - 1].endTime.mday);
    lcd_putchar(135, 120, BLACK, '/');
    lcd_putint(145, 120, BLACK, parking[placeNum - 1].endTime.mon);
    lcd_putchar(160, 120, BLACK, '/');
    lcd_putint(170, 120, BLACK, parking[placeNum - 1].endTime.year);
    lcd_putchar(185, 120, BLACK, ' ');
    lcd_putint(195, 120, BLACK, parking[placeNum - 1].endTime.hour);
    lcd_putchar(210, 120, BLACK, ':');
    lcd_putint(220, 120, BLACK, parking[placeNum - 1].endTime.min);

    sw_delay_ms(2000);
    lcd_clear();
    drawParkingGrid();
}

void showTariffScreen(uint8 placeNum, uint16 credit)
{
    lcd_clear();

    // Dibujamos el rect�ngulo para la tarifa
    lcd_draw_box(0, 40, 319, 110, BLACK, 1);

    // T�tulo centrado
    lcd_puts(40, 24, BLACK, "TARIFA");

    // Informaci�n de tarifa dentro del rect�ngulo
    lcd_puts(24, 52, BLACK, "Precio por minuto: 0,01 euros");
    lcd_puts(24,76, BLACK, "Estancia minima:  20 minutos");
    lcd_puts(24, 100, BLACK, "Estancia maxima: 240 minutos");

    // Informaci�n de plaza
    lcd_puts_x2(72, 120, BLACK, "Plaza ");
    lcd_putint_x2(156, 120, BLACK, placeNum);//putint

    // Cr�dito
    //lcd_puts(60, 160, BLACK, "Credito:       euros");            putint para que funcione bien
    lcd_putint(60, 160, BLACK, credit/100);
    lcd_putint(96, 160, BLACK, credit%100);
    // Mensajes finales
    lcd_puts(70, 204, BLACK, "Inserte monedas");
    lcd_puts(32, 220, BLACK, "Pulse la pantalla para aceptar");
}
