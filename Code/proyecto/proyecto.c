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
#include "kernelcoop.h"

/////////////////////////////////////////////////////////////////////////////
// CONFIGURACION
/////////////////////////////////////////////////////////////////////////////

#define MAXPLACES              (10)
#define TICKS_PER_SEC          1000

/////////////////////////////////////////////////////////////////////////////
// Declaracion de tipos
/////////////////////////////////////////////////////////////////////////////

typedef struct
{
    uint32 startTime;   // Hora en la que se ocupó la plaza (en ticks del reloj)
    uint32 endTime;     // Hora en la que expiró la plaza (en ticks del reloj)
    boolean occupied;   // Indica si la plaza está ocupada
} parking_t;

/////////////////////////////////////////////////////////////////////////////
// Declaracion de funciones
/////////////////////////////////////////////////////////////////////////////

void setup( void );
void plotWelcomeScreen( void );

/////////////////////////////////////////////////////////////////////////////
// Declaracion de tareas
/////////////////////////////////////////////////////////////////////////////

void tsScanTask( void );
void coinsAcceptorTask( void );
void clockTask( void );
void coinsMoverTask( void );
void mainTask( void );
void ticketPrinterTask( void );

/////////////////////////////////////////////////////////////////////////////
// Declaracion de recursos
/////////////////////////////////////////////////////////////////////////////

parking_t parking[MAXPLACES];

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

void main( void )
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

    create_task( tsScanTask,          5 );    /* Crea las tareas de la aplicacion... */
    create_task( coinsAcceptorTask,   5 );    /* ... el kernel asigna la prioridad segun orden de creacion */
    create_task( mainTask,           10 );    /* ... las tareas mas frecuentes tienen mayor prioridad (criterio Rate-Monotonic-Scheduling) */
    create_task( coinsMoverTask,     10 );
    create_task( clockTask,          10 );
    create_task( ticketPrinterTask,  10 );

    timer0_open_tick( scheduler, TICKS_PER_SEC );  /* Instala scheduler como RTI del timer0  */

    while( 1 )
    {
        sleep();                /* Entra en estado IDLE, sale por interrupcion */
        dispacher();            /* Las tareas preparadas se ejecutan en esta hebra (background) en orden de prioridad */
    }
}

/*******************************************************************/

/* 
** Tarea principal, se activa cada 100 ms muestreando los mensajes enviados de otras tareas y actuando en consecuencia  
*/
void mainTask( void )  
{
    static boolean init = TRUE;
    static enum { demo_waiting, demo_acceptCoins } state;
    static uint16 credit;
    static uint16 ticks;

    if( init )
    {
        init   = FALSE;
        plotWelcomeScreen();
        credit = 0;
        state = demo_waiting;
    }
    else switch( state )
    {
        case demo_waiting:                        /* Estado en donde rechaza monedas y espera la pulsacion de la pantalla */
            if( tsPressedMsg.flag )                    /* Chequea si se ha pulsado la pantalla (mensaje recibido de la tarea tsPressedTask) */
            {
                tsPressedMsg.flag = FALSE;                 /* Marca el mensaje como leido */
                lcd_clear();                               /* Borra pantalla */
                state = demo_acceptCoins;                  /* Salta al estado demo_acceptCoins ... */
                ticks = 500;                               /* ... en el que debera permanecer un maximo de 500 ticks sin no hay actividad */
            }    
            if( coinAcceptorMsg.flag )                /* Chequea si se ha introducido una moneda (mensaje recibido de la tarea coinAcceptorTask) */
            {
                coinAcceptorMsg.flag = FALSE;              /* Marca el mensaje como leido */
                coinsMoverMsg.accept = FALSE;              /* Envia un mensaje para que la moneda se devuelva */
                coinsMoverMsg.flag   = TRUE;
            }    
            break;
        case demo_acceptCoins:                    /* Estado en deonde acepta monedas y espera la pulsacion de la pantalla durante un tiempo maximo */
            if( !(--ticks) )                           /* Decrementa ticks y chequea si ha permanecido en este estado el tiempo maximo */
            {
                plotWelcomeScreen();                       /* Visualiza pantalla inicial */
                state = demo_waiting;                      /* Salta al estado demo_waiting */
            }
            if( tsPressedMsg.flag )                    /* Chequea si se ha pulsado la pantalla (mensaje recibido de la tarea tsPressedTask) */
            {
                tsPressedMsg.flag = FALSE;                /* Marca el mensaje como leido */
                coinsMoverMsg.accept = TRUE;              /* Envia un mensaje para que las monedas se acepten */
                coinsMoverMsg.flag   = TRUE;
                credit = 0;                               /* Pone el saldo a 0 */
                plotWelcomeScreen();                      /* Visualiza pantalla inicial */
                state = demo_waiting;                     /* Salta al estado demo_waiting */
            }    
            if( coinAcceptorMsg.flag )                /* Chequea si se ha introducido una moneda (mensaje recibido de la tarea coinAcceptorTask) */
            {
                coinAcceptorMsg.flag   = FALSE;           /* Marca el mensaje como leido */
                credit += coinAcceptorMsg.cents;          /* Incrementa el saldo */
                lcd_putint_x2( 24, 48, BLACK, credit );   /* Muestra el saldo en pantalla */
                ticks = 500;                              /* Restaura el tiempo maximo sin actividad que permanece en este estado */
            }
            break;
    }
}

/* 
** Emula el comportamiento de la impresora de tickets:
**   Cada segundo muestrea si ha recibido un mensaje de la tarea principal enviar a traves de la UART el texto del ticket del aparcamiento elegido
*/
void ticketPrinterTask( void )
{
    static uint32 lastPrintTick = 0;

    // Solo ejecuta la impresión del ticket cada segundo
        if (printTicketFlag) {
            // Enviar el ticket por UART
            uart0_puts("Ticket del parking: 0.10 EUR\n");  // Ejemplo simple
            printTicketFlag = FALSE;  // Resetea el flag de impresión
        }
}

/* 
** Cada segundo visualiza la fecha/hora en la pantalla y libera aquellas plazas cuya hora de finalizacion haya pasado
*/
void clockTask( void )  
{
    static uint32 ticks = 0;
    uint32 i=0;
    ticks++;  // Incrementa el contador cada vez que se llama la tarea

        // Actualiza la visualización de la hora en pantalla
        lcd_clear();
        //Saco la hora
        rtc_time_t rtc_time;
        rtc_gettime(&rtc_time);
        lcd_putint_x2( 24, 48, BLACK, rtc_time ); // Muestra la hora actual

        // Liberar plazas de parking cuya hora de finalización haya pasado

        for (; i < MAXPLACES; i++) {
            if (parking[i].occupied && rtc_time >= parking[i].endTime) {
                // La plaza ha expirado, se libera
                parking[i].occupied = FALSE;  // Marca como libre
                parking[i].startTime = 0;     // Resetea el tiempo de inicio
                parking[i].endTime = 0;       // Resetea el tiempo de finalización
                // Aquí puedes agregar más lógica como notificar al usuario
                lcd_clear();
                lcd_puts(24, 48, BLACK,"Plaza Liberada");
            }
        }

}

/* 
** Cada 50 ms muestrea la touchscreen y envia un mensaje a la tarea principal con la posicion del lugar pulsado
*/
void tsScanTask( void )  
{
    static boolean init = TRUE;
    static enum { wait_keydown, scan, wait_keyup } state;
    uint16 x,y;
    if( init )
    {
        init  = FALSE;
        state = wait_keydown;
    }
    else switch( state )
    {
        case wait_keydown:                        /* Estado esperando la presion de la pantalla */
            if( ts_pressed() )                        /* Chequea la pantalla esta presionada */
                state = scan;                         /* Salta al estado scan */
            break;
        case scan:
        	ts_getpos(x,y);                          /* Estado en que escanea la pantalla */
            tsPressedMsg.x = x;             /* Obtiene la posicion X */
            tsPressedMsg.y = y;             /* Obtiene la posicion Y */
            tsPressedMsg.flag = TRUE;                /* Marca el mensaje como valido */
            state = wait_keyup;                      /* Cambia al estado wait_keyup */
            break;
        case wait_keyup:                           /* Estado esperando que se deje de presionar la pantalla */
            if( !ts_pressed() )                     /* Chequea que la pantalla ya no esta presionada */
                state = wait_keydown;                /* Vuelve al estado inicial */
            break;
    }
}

/* 
** Cada 100 ms chequea si ha llegado alguna moneda al receptor y si es asi la envia a coinsMoverTask
*/
#define COIN_1_EURO   100    // 1€ en céntimos
#define COIN_50_CENT  50     // 0.50€ en céntimos
#define COIN_20_CENT  20     // 0.20€ en céntimos
#define COIN_10_CENT  10     // 0.10€ en céntimos

int totalCoins = 0;  // Variable global para almacenar el total de monedas introducidas

void coinsAcceptorTask(void) {
    char key = keypad_scan(); // Función que simula la entrada del teclado

    switch(key) {
        case '1':
            totalCoins += COIN_1_EURO;
            break;
        case '2':
            totalCoins += COIN_50_CENT;
            break;
        case '3':
            totalCoins += COIN_20_CENT;
            break;
        case '4':
            totalCoins += COIN_10_CENT;
            break;
        default:
            break;
    }

    // Actualizar pantalla o LED con el total de monedas insertadas
    updateDisplayWithCoins(totalCoins);
}

/* 
** Gestiona el movimiento de las monedas que entran o salen del parking (aceptadas o rechazadas)
*/
void coinsMoverTask( void ) 
{
    if( coinsMoverMsg.flag )
    {
        coinsMoverMsg.flag = FALSE;
        if (coinsMoverMsg.accept) {
            // Acepta monedas para incrementar el saldo
        } else {
            // Rechaza las monedas y las devuelve
        }
    }
}

/*******************************************************************/

void setup( void )
{
    // Inicialización de variables o configuraciones necesarias
	int i;
    for (i = 0; i < MAXPLACES; i++) {
        parking[i].occupied = FALSE;
    }
    printTicketFlag = FALSE;
}

/*******************************************************************/

void plotWelcomeScreen( void )
{
    lcd_clear();
    lcd_puts(24, 48, BLACK,"Bienvenido al Parking");
    lcd_puts(24, 76, BLACK,"Pase por favor.");
}
