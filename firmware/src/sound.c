#include "asf.h"
#include "sound.h"

#include <string.h>
#include <tc.h>
#include <dacc.h>

// ��������� �����
static uint16_t g_buffer[4096];
// ��������� �� ������, ����� � ������� ������ ������ � ������
static volatile uint16_t g_buffer_head, g_buffer_tail, g_buffer_size;

// ���������� ������ ������������ ���������� ����� ������ � ������
static inline uint16_t get_continuous_data_size_irq(void)
{
	return g_buffer_head <= g_buffer_tail ? g_buffer_tail - g_buffer_head : ARRAYSIZE(g_buffer) - g_buffer_head;
}

// ���������� ���������� ���������� ����� � ������
static inline uint16_t get_buffer_capacity(void)
{
	return g_buffer_size;
}

// �������� ������ � �����
static uint16_t copy_in_buffer(const uint16_t * data, uint16_t size)
{
	uint16_t tail = g_buffer_tail;
	
	// � ���� ����� �� ��������� ������� ��������
	uint32_t to_copy = min(size, ARRAYSIZE(g_buffer) - tail);
	memcpy((void *)(g_buffer + tail), (void *)data, (size_t)(to_copy << 1));
	
	if (to_copy < size)
	{
		// ����� ������ ����� ����������� � ������ ������
		memcpy(g_buffer, data + to_copy, (size - to_copy) << 1);
		tail = size - to_copy;
	}		
	else
	{
		// ��� ������ ������� ���������� � ����� ������
		tail += size;
	}
	
	return tail;
}

// ���������� ��������� �� ������ ������, ���������� ������� ������
static inline void move_head_irq(uint16_t size)
{
	g_buffer_head += size;
	g_buffer_size += size;
	if (g_buffer_head >= ARRAYSIZE(g_buffer) && g_buffer_head > g_buffer_tail)
		g_buffer_head -= ARRAYSIZE(g_buffer);
}

// ������ ����������� ��� ������ � PDC

// �������� ������ ������
static pdc_packet_t g_pdc_packet;
// ������ ������ � ������ PDC
static volatile uint32_t g_pdc_packet_size;
// ���� �� 0, �� PDC ��� ��� �������
static volatile uint8_t g_pdc_active;

// ��������� ���������� �� DACC
static inline void disable_irq(void)
{
	dacc_disable_interrupt(DACC, DACC_IER_ENDTX);
}

// ���� PDC �������, �� ��������� ���������� �� DACC
static inline void restore_irq(void)
{
	if (g_pdc_active)
		dacc_enable_interrupt(DACC, DACC_IER_ENDTX);
}

#define DACC_PDC_PACKET_MAX_SIZE 512

// ���������� ���������� DACC
// ����������, ����� ��������� ����� ��� ��������� PDC
void DACC_Handler(void)
{
	// ������� ����������
	uint32_t status = dacc_get_interrupt_status(DACC);

	if (status & DACC_IER_ENDTX)
	{
		// ��������� ����� ���������, ������������ �����
		move_head_irq(g_pdc_packet_size);
		uint16_t data_size = get_continuous_data_size_irq();

		if (data_size == 0)
		{
			// ��������� ������
			g_pdc_packet_size = 0;
			g_pdc_active = 0;
			// ��������� ���������� �� DACC
			dacc_disable_interrupt(DACC, DACC_IDR_ENDTX);
		}
		else
		{
			// ����������� PDC �� �������� ���������� ������
			g_pdc_packet.ul_addr = (uint32_t)(g_buffer + g_buffer_head);
			g_pdc_packet_size = min(DACC_PDC_PACKET_MAX_SIZE, data_size);
			g_pdc_packet.ul_size = g_pdc_packet_size;
			
			// ����������
			pdc_tx_init(PDC_DACC, &g_pdc_packet, NULL);
		}
	}
}

// ����������� ����� 0 TC �� ��������� � ��������� ��������
static void tc_adjust_frequency(uint16_t frequency)
{
	uint32_t divider = sysclk_get_cpu_hz() / frequency;
	divider >>= 1;

	tc_write_ra(TC0, 0, divider);
	tc_write_rc(TC0, 0, divider + 1);
}

// �������������� ������
void sound_init_hardware(void)
{
	// �������� ������ TC
	sysclk_enable_peripheral_clock(ID_TC0);

	// ��������� TC0 � ����� waveform
	tc_init(TC0, 0,
			TC_CMR_TCCLKS_TIMER_CLOCK1
			| TC_CMR_WAVE /* Waveform mode is enabled */
			| TC_CMR_ACPA_SET /* RA Compare Effect: set */
			| TC_CMR_ACPC_CLEAR /* RC Compare Effect: clear */
			| TC_CMR_CPCTRG /* UP mode with automatic trigger on RC Compare */
	);

	// ������������� �����-�� ������� ��� TC, �� ����� �����
	tc_adjust_frequency(8000);

	// ��������� ������
	tc_start(TC0, 0);

	// �������� ������ DACC
	sysclk_enable_peripheral_clock(ID_DACC);

	// ���������� ��� �������� DACC
	dacc_reset(DACC);

	// ��������� DACC � ����� "half word"
	dacc_set_transfer_mode(DACC, 0);

	// ��������� �������� �������
	dacc_set_power_save(DACC, 0, 0);

	// �������� ����� TC0 � �������� ��������
	dacc_set_trigger(DACC, 1);

	// ��������� TAG � �������� ������ ����� DACC
	dacc_set_channel_selection(DACC, 1);

	// �������� ������ ����� DACC
	dacc_enable_channel(DACC, 1);

	// ��������� ���������� ������ ������
	dacc_set_analog_control(DACC, DACC_ACR_IBCTLCH0(0x02) | DACC_ACR_IBCTLCH1(0x02) | DACC_ACR_IBCTLDACCORE(0x01));
	
	// ������� ��� ���������� DACC ��������� ���������
	NVIC_DisableIRQ(DACC_IRQn);
	NVIC_ClearPendingIRQ(DACC_IRQn);
	NVIC_EnableIRQ(DACC_IRQn);

	// ��������� ������������� PDC ��� ��������� ������ � DACC
	pdc_enable_transfer(PDC_DACC, PERIPH_PTCR_TXTEN);
	
	// ������������� ���������� ����������
	g_pdc_packet_size = 0;
	g_pdc_active = 0;
}

// ����������� ������������ ��� ������ ����� ��������� �������
void sound_start(uint16_t frequency)
{
	// �� ������ ������ ����
	sound_stop();
	// ������������ ������
	tc_adjust_frequency(frequency);
}	

// ������������ ����� �����
void sound_stop(void)
{
	// ���������� �� DACC ���������
	dacc_disable_interrupt(DACC, DACC_IER_ENDTX);

	g_pdc_packet_size = 0;
	g_buffer_head = g_buffer_tail = 0;
	g_buffer_size = ARRAYSIZE(g_buffer);
	g_pdc_active = 0;
}

// ���������� ��������� ������ ������ � ������� �� ����� �����
uint8_t sound_data(const uint16_t * data, uint16_t size)
{
	// ��������� ������� ������
	uint16_t capacity = get_buffer_capacity();
	if (capacity < size)
		return 0;
		
	// �������� ������ � �����
	uint16_t tail = copy_in_buffer(data, size);
	// ������������ ��������� ������
	disable_irq();
	g_buffer_tail = tail;
	g_buffer_size -= size;
	if (g_buffer_head >= ARRAYSIZE(g_buffer))
		g_buffer_head -= ARRAYSIZE(g_buffer);
	g_pdc_active = 1;
	restore_irq();
	
	// ��� ������ ���������
	return 1;
}
