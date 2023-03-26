#include <stdint.h>
#include <stdio.h>
#include "hashpipe.h"
#include "hashpipe_databuf.h"

#define CACHE_ALIGNMENT         8

#define PKT_SIZE                8200
#define PKT_NUM_PER_BLOCK       16384
#define N_BLOCKS_IN             32      // We use 8200*16384*32 = 4GB for input blocks.

#define SPECTRA_SIZE            8192
#define SPECTRA_NUM_PER_BLOCK   8192
#define N_BLOCKS_OUT            32      // We use 8192*2*8192*32 = 4GBMB for output blocks.

/* INPUT BUFFER STRUCTURES*/
typedef struct input_block_header {
   uint64_t mcnt;            
} input_block_header_t;

typedef uint8_t input_header_cache_alignment[
   CACHE_ALIGNMENT - (sizeof(input_block_header_t)%CACHE_ALIGNMENT)
];

typedef struct adc_pkt {
    uint64_t cnt;
    uint8_t raw_adc_data[PKT_SIZE - sizeof(uint64_t)];
} adc_pkt_t;

typedef struct input_block {
   input_block_header_t header;
   input_header_cache_alignment padding; // Maintain cache alignment
   adc_pkt_t adc[PKT_NUM_PER_BLOCK];
} input_block_t;

typedef struct input_databuf {
   hashpipe_databuf_t header;
   input_header_cache_alignment padding;
   input_block_t block[N_BLOCKS_IN];
} input_databuf_t;

/* OUTPUT BUFFER STRUCTURES*/
typedef struct output_block_header {
   uint64_t mcnt;            
} output_block_header_t;

typedef uint8_t output_header_cache_alignment[
   CACHE_ALIGNMENT - (sizeof(output_block_header_t)%CACHE_ALIGNMENT)
];

typedef struct spectra_frame {
    uint64_t cnt;
    uint16_t data[SPECTRA_SIZE];
} spectra_frame_t;

typedef struct output_block {
   output_block_header_t header;
   output_header_cache_alignment padding; // Maintain cache alignment
   spectra_frame_t spectra[SPECTRA_NUM_PER_BLOCK];
} output_block_t;

typedef struct output_databuf {
   hashpipe_databuf_t header;
   output_header_cache_alignment padding;
   output_block_t block[N_BLOCKS_OUT];
} output_databuf_t;

/*
 * INPUT BUFFER FUNCTIONS
 */
hashpipe_databuf_t *input_databuf_create(int instance_id, int databuf_id);

static inline input_databuf_t *input_databuf_attach(int instance_id, int databuf_id)
{
    return (input_databuf_t *)hashpipe_databuf_attach(instance_id, databuf_id);
}

static inline int input_databuf_detach(input_databuf_t *d)
{
    return hashpipe_databuf_detach((hashpipe_databuf_t *)d);
}

static inline void input_databuf_clear(input_databuf_t *d)
{
    hashpipe_databuf_clear((hashpipe_databuf_t *)d);
}

static inline int input_databuf_block_status(input_databuf_t *d, int block_id)
{
    return hashpipe_databuf_block_status((hashpipe_databuf_t *)d, block_id);
}

static inline int input_databuf_total_status(input_databuf_t *d)
{
    return hashpipe_databuf_total_status((hashpipe_databuf_t *)d);
}

static inline int input_databuf_wait_free(input_databuf_t *d, int block_id)
{
    return hashpipe_databuf_wait_free((hashpipe_databuf_t *)d, block_id);
}

static inline int input_databuf_busywait_free(input_databuf_t *d, int block_id)
{
    return hashpipe_databuf_busywait_free((hashpipe_databuf_t *)d, block_id);
}

static inline int input_databuf_wait_filled(input_databuf_t *d, int block_id)
{
    return hashpipe_databuf_wait_filled((hashpipe_databuf_t *)d, block_id);
}

static inline int input_databuf_busywait_filled(input_databuf_t *d, int block_id)
{
    return hashpipe_databuf_busywait_filled((hashpipe_databuf_t *)d, block_id);
}

static inline int input_databuf_set_free(input_databuf_t *d, int block_id)
{
    return hashpipe_databuf_set_free((hashpipe_databuf_t *)d, block_id);
}

static inline int input_databuf_set_filled(input_databuf_t *d, int block_id)
{
    return hashpipe_databuf_set_filled((hashpipe_databuf_t *)d, block_id);
}

/*
 * OUTPUT BUFFER FUNCTIONS
 */
hashpipe_databuf_t *output_databuf_create(int instance_id, int databuf_id);

static inline output_databuf_t *output_databuf_attach(int instance_id, int databuf_id)
{
    return (output_databuf_t *)hashpipe_databuf_attach(instance_id, databuf_id);
}

static inline int output_databuf_detach(output_databuf_t *d)
{
    return hashpipe_databuf_detach((hashpipe_databuf_t *)d);
}

static inline void output_databuf_clear(output_databuf_t *d)
{
    hashpipe_databuf_clear((hashpipe_databuf_t *)d);
}

static inline int output_databuf_block_status(output_databuf_t *d, int block_id)
{
    return hashpipe_databuf_block_status((hashpipe_databuf_t *)d, block_id);
}

static inline int output_databuf_total_status(output_databuf_t *d)
{
    return hashpipe_databuf_total_status((hashpipe_databuf_t *)d);
}

static inline int output_databuf_wait_free(output_databuf_t *d, int block_id)
{
    return hashpipe_databuf_wait_free((hashpipe_databuf_t *)d, block_id);
}

static inline int output_databuf_busywait_free(output_databuf_t *d, int block_id)
{
    return hashpipe_databuf_busywait_free((hashpipe_databuf_t *)d, block_id);
}

static inline int output_databuf_wait_filled(output_databuf_t *d, int block_id)
{
    return hashpipe_databuf_wait_filled((hashpipe_databuf_t *)d, block_id);
}

static inline int output_databuf_busywait_filled(output_databuf_t *d, int block_id)
{
    return hashpipe_databuf_busywait_filled((hashpipe_databuf_t *)d, block_id);
}

static inline int output_databuf_set_free(output_databuf_t *d, int block_id)
{
    return hashpipe_databuf_set_free((hashpipe_databuf_t *)d, block_id);
}

static inline int output_databuf_set_filled(output_databuf_t *d, int block_id)
{
    return hashpipe_databuf_set_filled((hashpipe_databuf_t *)d, block_id);
}