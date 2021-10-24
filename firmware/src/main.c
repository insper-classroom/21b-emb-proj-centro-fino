/************************************************************************/
/* includes                                                             */
/************************************************************************/
#include <asf.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "conf_board.h"
#include "conf_uart_serial.h"

#include "gfx_mono_ug_2832hsweg04.h"
#include "gfx_mono_text.h"
#include "sysfont.h"

// LEDs
#define LED_PIO      PIOC
#define LED_PIO_ID   ID_PIOC
#define LED_IDX      8
#define LED_IDX_MASK (1 << LED_IDX)



/* Botao da placa */
#define BUT_PIO     PIOA
#define BUT_PIO_ID  ID_PIOA
#define BUT_PIO_PIN 11
#define BUT_PIO_PIN_MASK (1 << BUT_PIO_PIN)

// Botão1 PD28 (por enquanto oled) PA2
#define BUT1_PIO      PIOA
#define BUT1_PIO_ID   ID_PIOA
#define BUT1_IDX      2
#define BUT1_IDX_MASK (1 << BUT1_IDX)

// Botão1 PC31 (por enquanto oled) PA3
#define BUT2_PIO	   PIOA
#define BUT2_PIO_ID	   ID_PIOA
#define BUT2_IDX       3
#define BUT2_IDX_MASK  (1u << BUT2_IDX)

// Botão1 PC31 (por enquanto oled)  PA4
#define BUT3_PIO	   PIOA
#define BUT3_PIO_ID	   ID_PIOA
#define BUT3_IDX       4
#define BUT3_IDX_MASK  (1u << BUT3_IDX)

// Botão1 PC31 (por enquanto oled)  PA21
#define BUT4_PIO	   PIOA
#define BUT4_PIO_ID	   ID_PIOA
#define BUT4_IDX       21
#define BUT4_IDX_MASK  (1u << BUT4_IDX)

#define AFEC_POT AFEC0
#define AFEC_POT_ID ID_AFEC0
#define AFEC_POT_CHANNEL 0 // Canal do pino PD30


/** RTOS  */
#define TASK_OLED_STACK_SIZE                 (1024*6/sizeof(portSTACK_TYPE))
#define TASK_OLED_STACK_PRIORITY             (tskIDLE_PRIORITY)

#define TASK_LCD_STACK_SIZE                  (1024*6/sizeof(portSTACK_TYPE))
#define TASK_LCD_STACK_PRIORITY              (tskIDLE_PRIORITY)

#define TASK_BLUETOOTH_STACK_SIZE            (4096/sizeof(portSTACK_TYPE))
#define TASK_BLUETOOTH_STACK_PRIORITY        (tskIDLE_PRIORITY)

// usart (bluetooth ou serial) -> descomente para enviar dados pela serial debug
#define DEBUG_SERIAL

#ifdef DEBUG_SERIAL
#define USART_COM USART1
#define USART_COM_ID ID_USART1
#else
#define USART_COM USART0
#define USART_COM_ID ID_USART0
#endif

typedef struct {
	uint value;
} adcData;

typedef struct{
	uint button;
	int status;
} press;

/************************************************************************/
/* Globals                                                              */
/************************************************************************/

QueueHandle_t xQueueADC;
QueueHandle_t xQueueBut;

SemaphoreHandle_t xSemaphore;

/** The conversion data is done flag */
volatile bool g_is_conversion_done = false;

/** The conversion data value */
volatile uint32_t g_ul_value = 0;


extern void vApplicationStackOverflowHook(xTaskHandle *pxTask,  signed char *pcTaskName);
extern void vApplicationIdleHook(void);
extern void vApplicationTickHook(void);
extern void vApplicationMallocFailedHook(void);
extern void xPortSysTickHandler(void);

/** prototypes */
static void BUT_init(void);
void but1_callback(void);
void but2_callback(void);
void but3_callback(void);
void but4_callback(void);


/************************************************************************/
/* RTOS application funcs                                               */
/************************************************************************/

extern void vApplicationStackOverflowHook(xTaskHandle *pxTask, signed char *pcTaskName) {
	/* If the parameters have been corrupted then inspect pxCurrentTCB to
	* identify which task has overflowed its stack.
	*/
	printf("stack overflow %x %s\r\n", pxTask, (portCHAR *)pcTaskName);
	for (;;) {	}
}

/* This function is called by FreeRTOS idle task */
extern void vApplicationIdleHook(void) {
	pmc_sleep(SAM_PM_SMODE_SLEEP_WFI);
}

/* This function is called by FreeRTOS each tick */
extern void vApplicationTickHook(void) { }

extern void vApplicationMallocFailedHook(void) {
	/* Called if a call to pvPortMalloc() fails because there is insufficient
	free memory available in the FreeRTOS heap.  pvPortMalloc() is called
	internally by FreeRTOS API functions that create tasks, queues, software
	timers, and semaphores.  The size of the FreeRTOS heap is set by the
	configTOTAL_HEAP_SIZE configuration constant in FreeRTOSConfig.h. */

	/* Force an assert. */
	configASSERT( ( volatile void * ) NULL );
}

/************************************************************************/
/* handlers / callbacks                                                 */
/************************************************************************/

/**
* \brief AFEC interrupt callback function.
*/

static void AFEC_pot_Callback(void){
  g_ul_value = afec_channel_get_value(AFEC_POT, AFEC_POT_CHANNEL);
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  xSemaphoreGiveFromISR(xSemaphore, &xHigherPriorityTaskWoken);
}

/************************************************************************/
/* USART UTIL                                                            */
/************************************************************************/
uint32_t usart_puts(uint8_t *pstring) {
	uint32_t i ;

	while(*(pstring + i))
	if(uart_is_tx_empty(USART_COM))
	usart_serial_putchar(USART_COM, *(pstring+i++));
}

void usart_put_string(Usart *usart, char str[]) {
	usart_serial_write_packet(usart, str, strlen(str));
}

int usart_get_string(Usart *usart, char buffer[], int bufferlen, uint timeout_ms) {
	uint timecounter = timeout_ms;
	uint32_t rx;
	uint32_t counter = 0;

	while( (timecounter > 0) && (counter < bufferlen - 1)) {
		if(usart_read(usart, &rx) == 0) {
			buffer[counter++] = rx;
		}
		else{
			timecounter--;
			vTaskDelay(1);
		}
	}
	buffer[counter] = 0x00;
	return counter;
}

void usart_send_command(Usart *usart, char buffer_rx[], int bufferlen,char buffer_tx[], int timeout) {
	usart_put_string(usart, buffer_tx);
	usart_get_string(usart, buffer_rx, bufferlen, timeout);
}

void config_usart0(void) {
	sysclk_enable_peripheral_clock(ID_USART0);
	usart_serial_options_t config;
	config.baudrate = 9600;
	config.charlength = US_MR_CHRL_8_BIT;
	config.paritytype = US_MR_PAR_NO;
	config.stopbits = false;
	usart_serial_init(USART0, &config);
	usart_enable_tx(USART0);
	usart_enable_rx(USART0);

	// RX - PB0  TX - PB1
	pio_configure(PIOB, PIO_PERIPH_C, (1 << 0), PIO_DEFAULT);
	pio_configure(PIOB, PIO_PERIPH_C, (1 << 1), PIO_DEFAULT);
}

void but1_callback(void) {
	press press1;
	press1.button = 1;
	press1.status = !pio_get(BUT1_PIO, PIO_INPUT, BUT1_IDX_MASK);
	xQueueSendFromISR(xQueueBut, &press1, 0);
}
void but2_callback() {
	press press2;
	press2.button = 2;
	press2.status = !pio_get(BUT2_PIO, PIO_INPUT, BUT2_IDX_MASK);
	xQueueSendFromISR( xQueueBut, &press2, 0);	
}
void but3_callback() {
	press press3;
	press3.button = 3;
	press3.status = !pio_get(BUT3_PIO, PIO_INPUT, BUT3_IDX_MASK);
	xQueueSendFromISR( xQueueBut, &press3, 0);	
}
void but4_callback() {
	press press4;
	press4.button = 4;
	press4.status = !pio_get(BUT4_PIO, PIO_INPUT, BUT4_IDX_MASK);
	xQueueSendFromISR( xQueueBut, &press4, 0);	
}


/************************************************************************/
/* TASKS                                                                */
/************************************************************************/

static void task_oled(void *pvParameters) {
	gfx_mono_ssd1306_init();
	gfx_mono_draw_string("Exemplo RTOS", 0, 0, &sysfont);
	
	adcData adc;
	
	for (;;)  {
		//Busca um novo valor na fila do ADC!
		//Formata e imprimi no LCD o dado
		if (xQueueReceive( xQueueADC, &(adc), ( TickType_t) 100 / portTICK_PERIOD_MS)) {
			char b[512];
			sprintf(b, "%04d", adc.value);
			gfx_mono_draw_string(b, 0, 20, &sysfont);
		}
	}
}

int hc05_init(void) {
	char buffer_rx[128];
	usart_send_command(USART_COM, buffer_rx, 1000, "AT", 100);
	vTaskDelay( 500 / portTICK_PERIOD_MS);
	usart_send_command(USART_COM, buffer_rx, 1000, "AT", 100);
	vTaskDelay( 500 / portTICK_PERIOD_MS);
	usart_send_command(USART_COM, buffer_rx, 1000, "AT+dani", 100);
	vTaskDelay( 500 / portTICK_PERIOD_MS);
	usart_send_command(USART_COM, buffer_rx, 1000, "AT", 100);
	vTaskDelay( 500 / portTICK_PERIOD_MS);
	usart_send_command(USART_COM, buffer_rx, 1000, "AT+senha", 100);
}

/************************************************************************/
/* funcoes                                                              */
/************************************************************************/

void io_init(void) {
	// Ativa PIOs
	pmc_enable_periph_clk(LED_PIO_ID);
	pmc_enable_periph_clk(BUT1_PIO_ID);
	pmc_enable_periph_clk(BUT2_PIO_ID);
	pmc_enable_periph_clk(BUT3_PIO_ID);
	pmc_enable_periph_clk(BUT4_PIO_ID);

	// Configura Pinos
	pio_configure(LED_PIO, PIO_OUTPUT_0, LED_IDX_MASK, PIO_DEFAULT | PIO_DEBOUNCE);
	
	pio_configure(BUT1_PIO, PIO_INPUT, BUT1_IDX_MASK, PIO_PULLUP);
	pio_configure(BUT2_PIO, PIO_INPUT, BUT2_IDX_MASK, PIO_PULLUP);
	pio_configure(BUT3_PIO, PIO_INPUT, BUT3_IDX_MASK, PIO_PULLUP);
	pio_configure(BUT4_PIO, PIO_INPUT, BUT4_IDX_MASK, PIO_PULLUP);	
	
	pio_handler_set(BUT1_PIO, BUT1_PIO_ID, BUT1_IDX_MASK, PIO_IT_FALL_EDGE, but1_callback);
	pio_handler_set(BUT2_PIO, BUT2_PIO_ID, BUT2_IDX_MASK, PIO_IT_FALL_EDGE, but2_callback);
	pio_handler_set(BUT3_PIO, BUT3_PIO_ID, BUT3_IDX_MASK, PIO_IT_FALL_EDGE, but3_callback);
	pio_handler_set(BUT4_PIO, BUT4_PIO_ID, BUT4_IDX_MASK, PIO_IT_FALL_EDGE, but4_callback);
	
	pio_set_debounce_filter(BUT1_PIO, BUT1_IDX_MASK, 80);
	pio_set_debounce_filter(BUT2_PIO, BUT2_IDX_MASK, 80);
	pio_set_debounce_filter(BUT3_PIO, BUT3_IDX_MASK, 80);
	pio_set_debounce_filter(BUT4_PIO, BUT4_IDX_MASK, 80);

	pio_enable_interrupt(BUT1_PIO, BUT1_IDX_MASK);
	pio_enable_interrupt(BUT2_PIO, BUT2_IDX_MASK);
	pio_enable_interrupt(BUT3_PIO, BUT3_IDX_MASK);
	pio_enable_interrupt(BUT4_PIO, BUT4_IDX_MASK);

	/* configura prioridae */
	NVIC_EnableIRQ(BUT1_PIO_ID);
	NVIC_EnableIRQ(BUT2_PIO_ID);
	NVIC_EnableIRQ(BUT3_PIO_ID);
	NVIC_EnableIRQ(BUT4_PIO_ID);

	NVIC_SetPriority(BUT1_PIO_ID, 4);
	NVIC_SetPriority(BUT2_PIO_ID, 4);
	NVIC_SetPriority(BUT3_PIO_ID, 4);
	NVIC_SetPriority(BUT4_PIO_ID, 4);
}

static void BUT_init(void) {


	/* conf botão como entrada */
}

static void config_AFEC_pot(Afec *afec, uint32_t afec_id, uint32_t afec_channel, afec_callback_t callback){
  /*************************************
  * Ativa e configura AFEC
  *************************************/
  /* Ativa AFEC - 0 */
  afec_enable(afec);

  /* struct de configuracao do AFEC */
  struct afec_config afec_cfg;

  /* Carrega parametros padrao */
  afec_get_config_defaults(&afec_cfg);

  /* Configura AFEC */
  afec_init(afec, &afec_cfg);

  /* Configura trigger por software */
  afec_set_trigger(afec, AFEC_TRIG_SW);

  /*** Configuracao específica do canal AFEC ***/
  struct afec_ch_config afec_ch_cfg;
  afec_ch_get_config_defaults(&afec_ch_cfg);
  afec_ch_cfg.gain = AFEC_GAINVALUE_0;
  afec_ch_set_config(afec, afec_channel, &afec_ch_cfg);

  /*
  * Calibracao:
  * Because the internal ADC offset is 0x200, it should cancel it and shift
  down to 0.
  */
  afec_channel_set_analog_offset(afec, afec_channel, 0x200);

  /***  Configura sensor de temperatura ***/
  struct afec_temp_sensor_config afec_temp_sensor_cfg;

  afec_temp_sensor_get_config_defaults(&afec_temp_sensor_cfg);
  afec_temp_sensor_set_config(afec, &afec_temp_sensor_cfg);
  
  /* configura IRQ */
  afec_set_callback(afec, afec_channel,	callback, 1);
  NVIC_SetPriority(afec_id, 4);
  NVIC_EnableIRQ(afec_id);
}

static void configure_console(void) {
	const usart_serial_options_t uart_serial_options = {
		.baudrate   = CONF_UART_BAUDRATE,
		.charlength = CONF_UART_CHAR_LENGTH,
		.paritytype = CONF_UART_PARITY,
		.stopbits   = CONF_UART_STOP_BITS,
	};

	/* Configure console UART. */
	stdio_serial_init(CONF_UART, &uart_serial_options);

	/* Specify that stdout should not be buffered. */
	setbuf(stdout, NULL);
}



/************************************************************************/
/* TASKS                                                                */
/************************************************************************/

void task_adc(void){
	/* inicializa e configura adc */
	config_AFEC_pot(AFEC_POT, AFEC_POT_ID, AFEC_POT_CHANNEL, AFEC_pot_Callback);
	/* Selecina canal e inicializa conversão */
	afec_channel_enable(AFEC_POT, AFEC_POT_CHANNEL);
	afec_start_software_conversion(AFEC_POT);
	
	adcData adc;

	while(1){
		if(xSemaphoreTake(xSemaphore, 0)){
			//printf("%d\n", g_ul_value);
			
			adc.value = g_ul_value;
			xQueueSend(xQueueADC, &adc, 0);
			vTaskDelay(500);
			
			afec_channel_enable(AFEC_POT, AFEC_POT_CHANNEL);
			afec_start_software_conversion(AFEC_POT);
		}
	}
}

void task_bluetooth(void) {
	printf("Task Bluetooth started \n");
	
	printf("Inicializando HC05 \n");
	config_usart0();
	hc05_init();

	// configura LEDs e Botões
	io_init();
	
	press   press_main;
	adcData adc;

	char eof = 'X';

	// Task não deve retornar.
	while(1) {
		if (xQueueReceive(xQueueBut, &(press_main), ( TickType_t )  1 / portTICK_PERIOD_MS)) {
			//printf("Button: %d   Status: %d\n", press_main.button, press_main.status);
			while(!usart_is_tx_ready(USART_COM)) {
				vTaskDelay(10 / portTICK_PERIOD_MS);
			}
			
			usart_write(USART_COM, press_main.button);
			
			while(!usart_is_tx_ready(USART_COM)) {
				vTaskDelay(10 / portTICK_PERIOD_MS);
			}
			
			usart_write(USART_COM, press_main.status);
			
			
			while(!usart_is_tx_ready(USART_COM)) {
				vTaskDelay(10 / portTICK_PERIOD_MS);
			}
			usart_write(USART_COM, eof);
		}
		
		//if (xQueueReceive(xQueueADC, &(adc), (TickType_t)100 / portTICK_PERIOD_MS)){
			//while(!usart_is_tx_ready(USART_COM)) {
				//vTaskDelay(10 / portTICK_PERIOD_MS);
			//}
			//usart_write(USART_COM, 'P');
//
			//while(!usart_is_tx_ready(USART_COM)) {
				//vTaskDelay(10 / portTICK_PERIOD_MS);
			//}
//
			//usart_write(USART_COM, adc.value);
			//while(!usart_is_tx_ready(USART_COM)) {
				//vTaskDelay(10 / portTICK_PERIOD_MS);
			//}
			//usart_write(USART_COM, eof);
//
		//}
		
		// dorme por 500 ms
		vTaskDelay(500 / portTICK_PERIOD_MS);
	}
}


/************************************************************************/
/* main                                                                 */
/************************************************************************/


int main(void) {
	/* Initialize the SAM system */
	sysclk_init();
	board_init();

	/* Initialize the console uart */
	configure_console();
	
	xQueueBut  = xQueueCreate(10, sizeof(press));
	xQueueADC  = xQueueCreate(10, sizeof(adcData));
	xSemaphore = xSemaphoreCreateBinary();


	/* Create task to make led blink */
	if (xTaskCreate(task_bluetooth, "BLT", TASK_BLUETOOTH_STACK_SIZE, NULL,	TASK_BLUETOOTH_STACK_PRIORITY, NULL) != pdPASS) {
		printf("Failed to create oled task \r\n");
	}
		

	/* Create task to control oled */
	if (xTaskCreate(task_oled, "oled", TASK_OLED_STACK_SIZE, NULL, TASK_OLED_STACK_PRIORITY, NULL) != pdPASS) {
	  printf("Failed to create oled task\r\n");
	}

	/* Create task to handler LCD */
	
	if (xTaskCreate(task_adc, "adc", TASK_LCD_STACK_SIZE, NULL, TASK_LCD_STACK_PRIORITY, NULL) != pdPASS) {
		printf("Failed to create test adc task\r\n");
	}
	
	/* Start the scheduler. */
	vTaskStartScheduler();

  /* RTOS não deve chegar aqui !! */
	while(1){
	}

	/* Will only get here if there was insufficient memory to create the idle task. */
	return 0;
}
