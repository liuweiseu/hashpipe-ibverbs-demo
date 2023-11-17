#include <stdint.h>
#include <stdio.h>
#include "hashpipe.h"
#include "hashpipe_databuf.h"

#define CACHE_ALIGNMENT         4096

#define IP_UDP_HDR_SIZE         42
#define RPKT_HDR_SIZE           22
#define RPKT_DAT_SIZE           8896//8128 //8192
#define RPKT_SIZE               (uint32_t)(IP_UDP_HDR_SIZE + RPKT_HDR_SIZE + RPKT_DAT_SIZE)
#define RPKTS_PER_BLOCK         (int)(65536)
#define N_BLOCKS_IN             16
// We use 8256*16384*32 = 4.03125GB for input blocks.
#define BLOCK_IN_DATA_SIZE      (RPKT_SIZE * RPKTS_PER_BLOCK )

#define SPECTRA_SIZE            8192
#define SPECTRAS_PER_BLOCK      16384
#define N_BLOCKS_OUT            32
// We use 8192*2*8192*32 = 4GBMB for output blocks.
#define BLOCK_OUT_DATA_SIZE     (SPECTRA_SIZE * SPECTRAS_PER_BLOCK * N_BLOCKS_OUT) 


/* INPUT BUFFER STRUCTURES*/
typedef struct input_block_header {
   uint8_t mcnt[8];            
} input_block_header_t;

typedef uint8_t input_header_cache_alignment[
   CACHE_ALIGNMENT - (sizeof(hashpipe_databuf_t)%CACHE_ALIGNMENT)
];

typedef struct adc_pkt {
    uint8_t ip_udp_hdr[IP_UDP_HDR_SIZE];
    input_block_header_t pkt_header;
    uint8_t adc_hdr[RPKT_HDR_SIZE-8];
    uint8_t adc_data[RPKT_DAT_SIZE];
} adc_pkt_t;

typedef struct input_block {
   adc_pkt_t adc_pkt[RPKTS_PER_BLOCK];
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
   spectra_frame_t spectra[SPECTRAS_PER_BLOCK];
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
