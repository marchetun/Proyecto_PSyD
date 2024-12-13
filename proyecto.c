/*-------------------------------------------------------------------
**
**  Aplicacion demo bajo un kernel de planificacion no expropiativo
**  de tareas coopertativas:
**    - Inicialmente espera la pulsacion de la pantalla, si se 
**      pulsa la tecla F (que emula la introduccion de 0,10 euros)
**      la devuelve (que se emula mediante el encendido de los leds
**      durante un segundo y la visualizacion de una D en segs)
**    - Si se pulsa la pantalla, la borra y por cada pulsacion 
**      de la tecla F (que emula la introduccion de 0,10 euros�)
**      acumula 10 centimos y los visualiza en la pantalla. 
**      Continua asi hasta que se pulse la pantalla, en cuyo
**      caso vuelve a la pantalla principal aceptando las monedas
**      (que se emula mediante el encendido de los leds
**      durante un segundo y la visualizacion de una A en segs);
**      o hasta que hayan pasado 5 segundos sin actividad.
**
**-----------------------------------------------------------------*/


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

parking_t parking;

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
   
}

/* 
** Cada segundo visualiza la fecha/hora en la pantalla y libera aquellas plazas cuya hora de finalizacion haya pasado
*/
void clockTask( void )  
{

}

/* 
** Cada 50 ms muestrea la touchscreen y envia un mensaje a la tarea principal con la posicion del lugar pulsado
*/
void tsScanTask( void )  
{
    static boolean init = TRUE;
    static enum { wait_keydown, scan, wait_keyup } state;
    
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
        case scan:                                /* Estado en que escanea la pantalla */
            ts_getpos( &tsPressedMsg.x, &tsPressedMsg.y );    /* Lee la pantalla */
            tsPressedMsg.flag = TRUE;                 /* Envia un mensaje con el valor de la posicion presionada */
            state = wait_keyup;                       /* Salta al estado wait_keyup */
            break;
        case wait_keyup:                          /* Estado esperando la depresion de la pantalla  */
            if( !ts_pressed() )                       /* Chequea si la pantalla ya no esta presionada */
                state = wait_keydown;                 /* Salta al estado wait_keydown */
            break;
    }
}

/* 
** Emula el comportamiento de un reconocedor de monedas:
**   Cada 50 ms muestrea el keypad y envia un mensaje a la tarea principal con el valor de la moneda asociada a la tecla
*/
void coinsAcceptorTask( void )  
{
    static boolean init = TRUE;
    static enum { wait_keydown, scan, wait_keyup } state;
    
    if( init )
    {
        init  = FALSE;
        state = wait_keydown;
    }
    else switch( state )
    {
        case wait_keydown:                        /* Estado esperando la presion teclas */
            if( keypad_pressed() )                    /* Chequea si hay una tecla presionada */
                state = scan;                         /* Salta al estado scan */
            break;
        case scan:                                /* Estado en que escanea el teclado */
            switch( keypad_scan() )                   /* Lee el teclado */
            {
                case KEYPAD_KEYF:                         /* La tecla F esta asociada a la moneda de 0,10 euros */
                    coinAcceptorMsg.cents = 10;           /* Envia un mensaje con el valor de la moneda */
                    coinAcceptorMsg.flag  = TRUE;
                    break;                
            }
            state = wait_keyup;                       /* Salta al estado wait_keyup */
            break;
        case wait_keyup:                          /* Estado esperando la depresion de teclas  */
            if( !keypad_pressed() )                   /* Chequea si la tecla ya no esta presionada */
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
void coinsMoverTask( void ) 
{
    static boolean init = TRUE;
    static enum { off, on } state;    
    static uint32 ticks;
    
    if( init )
    {
        init  = FALSE;
        state = off;
    }
    else switch( state )
    {
        case off:                                /* Estado con leds y 7-segs apagados */
            if( coinsMoverMsg.flag )                /* Chequea si ha recibido mensaje */
            {
                coinsMoverMsg.flag = FALSE;            /* Marca el mensaje como leido */
                if( coinsMoverMsg.accept )             /* Muestra el mensaje que corresponda en segs */
                    segs_putchar( 0xA );
                else
                    segs_putchar( 0xD );
                led_on( LEFT_LED );                    /* Enciende leds */
                led_on( RIGHT_LED );
                state = on;                            /* Salta al estado on ... */
                ticks = 10;                            /* ... en el que debera permanecer 10 ticks */
            }
            break;
        case on:                                /* Estado con leds y 7-segs encendidos */
            if( !(--ticks) )                        /* Decrementa ticks y chequea si ha permanecido en este estado el tiempo requerido */
            {  
                segs_off();                            /* Apaga segs */
                led_off( LEFT_LED );                   /* Apaga leds */
                led_off( RIGHT_LED );                                
                state = off;                           /* Salta al estado off */
            }
            break;
    }
}    

/*******************************************************************/

/* 
** Inicializa flags, mailboxes y variables globales
*/
void setup( void )
{
    coinAcceptorMsg.flag  = FALSE;
    coinAcceptorMsg.cents = 0;
    
    tsPressedMsg.flag     = FALSE;
    tsPressedMsg.x        = 0;
    tsPressedMsg.y        = 0;

    coinsMoverMsg.flag    = FALSE;
    coinsMoverMsg.accept  = FALSE;
    
    printTicketFlag       = FALSE;
}


void plotWelcomeScreen( void )
{   
    lcd_clear();
    lcd_puts_x2( 24, 48, BLACK, "Pulse la pantalla" );
    lcd_puts_x2( 24, 76, BLACK, "  para comenzar  " );
}
