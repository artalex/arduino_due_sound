#include "asf.h"
#include "sound.h"

#include <string.h>
#include <tc.h>
#include <dacc.h>

// Кольцевой буфер
static uint16_t g_buffer[4096];
// Указатели на голову, хвост и текущий размер данных в буфере
static volatile uint16_t g_buffer_head, g_buffer_tail, g_buffer_size;

// Возвращает размер непрерывного доступного куска данных в буфере
static inline uint16_t get_continuous_data_size_irq(void)
{
	return g_buffer_head <= g_buffer_tail ? g_buffer_tail - g_buffer_head : ARRAYSIZE(g_buffer) - g_buffer_head;
}

// Возвращает количество свободного места в буфере
static inline uint16_t get_buffer_capacity(void)
{
	return g_buffer_size;
}

// Копирует данные в буфер
static uint16_t copy_in_buffer(const uint16_t * data, uint16_t size)
{
	uint16_t tail = g_buffer_tail;
	
	// В этом месте не выполняем никаких проверок
	uint32_t to_copy = min(size, ARRAYSIZE(g_buffer) - tail);
	memcpy((void *)(g_buffer + tail), (void *)data, (size_t)(to_copy << 1));
	
	if (to_copy < size)
	{
		// Часть данных будут скопированы в начало буфера
		memcpy(g_buffer, data + to_copy, (size - to_copy) << 1);
		tail = size - to_copy;
	}		
	else
	{
		// Все данные удалось разместить в конце буфера
		tail += size;
	}
	
	return tail;
}

// Перемещает указатель на начало данных, увеличивая емкость буфера
static inline void move_head_irq(uint16_t size)
{
	g_buffer_head += size;
	g_buffer_size += size;
	if (g_buffer_head >= ARRAYSIZE(g_buffer) && g_buffer_head > g_buffer_tail)
		g_buffer_head -= ARRAYSIZE(g_buffer);
}

// Данные необходимые для работы с PDC

// Описание пакета данных
static pdc_packet_t g_pdc_packet;
// Размер данных в буфере PDC
static volatile uint32_t g_pdc_packet_size;
// Если не 0, то PDC все еще активен
static volatile uint8_t g_pdc_active;

// Запрещает прерывания от DACC
static inline void disable_irq(void)
{
	dacc_disable_interrupt(DACC, DACC_IER_ENDTX);
}

// Если PDC активен, то разрешает прерывания от DACC
static inline void restore_irq(void)
{
	if (g_pdc_active)
		dacc_enable_interrupt(DACC, DACC_IER_ENDTX);
}

#define DACC_PDC_PACKET_MAX_SIZE 512

// Обработчик прерываний DACC
// Вызывается, когда очередной пакет был отправлен PDC
void DACC_Handler(void)
{
	// Причина прерывания
	uint32_t status = dacc_get_interrupt_status(DACC);

	if (status & DACC_IER_ENDTX)
	{
		// Очередной пакет отправлен, модифицируем буфер
		move_head_irq(g_pdc_packet_size);
		uint16_t data_size = get_continuous_data_size_irq();

		if (data_size == 0)
		{
			// Кончились данные
			g_pdc_packet_size = 0;
			g_pdc_active = 0;
			// Запрещаем прерывания от DACC
			dacc_disable_interrupt(DACC, DACC_IDR_ENDTX);
		}
		else
		{
			// Настраиваем PDC на отправку очередного пакета
			g_pdc_packet.ul_addr = (uint32_t)(g_buffer + g_buffer_head);
			g_pdc_packet_size = min(DACC_PDC_PACKET_MAX_SIZE, data_size);
			g_pdc_packet.ul_size = g_pdc_packet_size;
			
			// Отправляем
			pdc_tx_init(PDC_DACC, &g_pdc_packet, NULL);
		}
	}
}

// Настраивает канал 0 TC на генерацию с указанной частотой
static void tc_adjust_frequency(uint16_t frequency)
{
	uint32_t divider = sysclk_get_cpu_hz() / frequency;
	divider >>= 1;

	tc_write_ra(TC0, 0, divider);
	tc_write_rc(TC0, 0, divider + 1);
}

// Инициализируем железо
void sound_init_hardware(void)
{
	// Включаем модуль TC
	sysclk_enable_peripheral_clock(ID_TC0);

	// Переводим TC0 в режим waveform
	tc_init(TC0, 0,
			TC_CMR_TCCLKS_TIMER_CLOCK1
			| TC_CMR_WAVE /* Waveform mode is enabled */
			| TC_CMR_ACPA_SET /* RA Compare Effect: set */
			| TC_CMR_ACPC_CLEAR /* RC Compare Effect: clear */
			| TC_CMR_CPCTRG /* UP mode with automatic trigger on RC Compare */
	);

	// Устанавливаем какую-то частоту для TC, не важно какую
	tc_adjust_frequency(8000);

	// Запускаем таймер
	tc_start(TC0, 0);

	// Включаем модуль DACC
	sysclk_enable_peripheral_clock(ID_DACC);

	// Сбрасываем все регистры DACC
	dacc_reset(DACC);

	// Переводим DACC в режим "half word"
	dacc_set_transfer_mode(DACC, 0);

	// Запрещаем экономию энергии
	dacc_set_power_save(DACC, 0, 0);

	// Выбираем канал TC0 в качестве триггера
	dacc_set_trigger(DACC, 1);

	// Запрещаем TAG и выбираем первый канал DACC
	dacc_set_channel_selection(DACC, 1);

	// Включаем первый канал DACC
	dacc_enable_channel(DACC, 1);

	// Настройка параметров работы канала
	dacc_set_analog_control(DACC, DACC_ACR_IBCTLCH0(0x02) | DACC_ACR_IBCTLCH1(0x02) | DACC_ACR_IBCTLDACCORE(0x01));
	
	// Очищаем все прерывания DACC ожидающие обработки
	NVIC_DisableIRQ(DACC_IRQn);
	NVIC_ClearPendingIRQ(DACC_IRQn);
	NVIC_EnableIRQ(DACC_IRQn);

	// Разрешаем использование PDC для пересылки данных в DACC
	pdc_enable_transfer(PDC_DACC, PERIPH_PTCR_TXTEN);
	
	// Инициализация глобальных переменных
	g_pdc_packet_size = 0;
	g_pdc_active = 0;
}

// Настраивает оборудование для вывода звука указанной частоты
void sound_start(uint16_t frequency)
{
	// На всякий случай стоп
	sound_stop();
	// Перенастроим таймер
	tc_adjust_frequency(frequency);
}	

// Останаливает вывод звука
void sound_stop(void)
{
	// Прерывания от DACC запрещены
	dacc_disable_interrupt(DACC, DACC_IER_ENDTX);

	g_pdc_packet_size = 0;
	g_buffer_head = g_buffer_tail = 0;
	g_buffer_size = ARRAYSIZE(g_buffer);
	g_pdc_active = 0;
}

// Отправляет очередную порцию данных в очередь на вывод звука
uint8_t sound_data(const uint16_t * data, uint16_t size)
{
	// Проверяем емкость буфера
	uint16_t capacity = get_buffer_capacity();
	if (capacity < size)
		return 0;
		
	// Копируем данные в буфер
	uint16_t tail = copy_in_buffer(data, size);
	// Модифицируем указатели буфера
	disable_irq();
	g_buffer_tail = tail;
	g_buffer_size -= size;
	if (g_buffer_head >= ARRAYSIZE(g_buffer))
		g_buffer_head -= ARRAYSIZE(g_buffer);
	g_pdc_active = 1;
	restore_irq();
	
	// Все прошло нормально
	return 1;
}
