#include "asf.h"

void sound_init_hardware(void);

void sound_start(uint16_t frequency);
void sound_stop(void);
uint8_t sound_data(const uint16_t * data, uint16_t size);
