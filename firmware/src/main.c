#include <asf.h>

#include "sound.h"

#define STRING_HEADER "-- Sound firmware --\r\n" \
"-- "BOARD_NAME" --\r\n" \
"-- Compiled: "__DATE__" "__TIME__" --\r"

static void configure_console(void)
{
	const usart_serial_options_t uart_serial_options = {
		.baudrate = 115200,
		.paritytype = UART_MR_PAR_NO
	};

	sysclk_enable_peripheral_clock(ID_UART);
	stdio_serial_init(UART, &uart_serial_options);
}


#include "sound_data.c"
static uint32_t g_sound_pos = 0;

int main (void)
{
    sysclk_init();
	board_init();
	configure_console();

    puts(STRING_HEADER);
	printf("CPU Frequency %li MHz\r\n", sysclk_get_cpu_hz() / 1000000);
	
	sound_init_hardware();
	sound_start(16000);

	puts("Work...\r");
	
	while (1)
	{
		// Отправляем кусками по 512 слов или то, что осталось
		uint32_t size = min(512, ARRAYSIZE(g_sound_data) - g_sound_pos);
		if (sound_data(g_sound_data + g_sound_pos, size))
		{
			// Данные успешно записаны в буфер
			g_sound_pos += size;
			if (g_sound_pos >= ARRAYSIZE(g_sound_data))
				g_sound_pos = 0;
		}
		else
		{
			// Нет места в буфере, пробуем еще раз	
		}			
	}
}
