#include <stdint.h>

typedef enum data_length_e
{
    B16 = 16,
    B18 = 14,
    B20 = 12,
    B24 = 8,
} data_length_t;

void initialize_wave(uint32_t sample_rate, uint16_t tone_frequency, double amplitude, data_length_t bitness);

int32_t *populate_buffer(uint16_t *num_bytes_xfer);

void print_buffer_stats();
