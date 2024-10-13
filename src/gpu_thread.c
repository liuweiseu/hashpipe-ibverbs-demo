/*
 * gpu_thread.c
 *
 * Routine to process data in GPU.
 * TODO: We do simple data processing without GPU for now, and we will use GPU later.
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

#include "hashpipe.h"
#include "databuf.h"

#include "hashpipe_ibverbs.h"

#include "gpulib.h"

#define CNT 65536

#ifndef ELAPSED_NS
#define ELAPSED_NS(start,stop) \
  (ELAPSED_S(start,stop)*1000*1000*1000+((stop).tv_nsec-(start).tv_nsec))
#endif

// Initialization function for Hashpipe.
// This function is called once when the thread is created
// args: Arugments passed in by hashpipe framework.
static int init(hashpipe_thread_args_t * args)
{
    hashpipe_status_t st = args->st;
    const char * status_key = args->thread_desc->skey;
    hashpipe_status_lock_safe(&st);
	hputu8(st.buf,"TPKTLOSS",0);
    hashpipe_status_unlock_safe(&st);
    return 0;
}

static void *run(hashpipe_thread_args_t * args)
{
    input_databuf_t *db_in = (input_databuf_t *)args->ibuf;
    output_databuf_t *db_out = (output_databuf_t *)args->obuf;
    hashpipe_status_t *st = &args->st;
    const char * thread_name = args->thread_desc->name;
    const char * status_key = args->thread_desc->skey;

    struct timespec ts_start;
    struct timespec ts_now;
    uint64_t ns_elapsed=0;
	int rv;
    int slot_id = 0;
    uint64_t cur_mcnt=0, pre_mcnt = 0;
    uint64_t pkt_loss = 0;
    uint64_t r_pkt_loss = 0;
    int curblock_in=0;
    int curblock_out=0;
    int first_pkt = 0;

    double gbps = 0;

    void *gpu_buf0, *gpu_buf1;
	void *host_buf;
    uint64_t size = RPKT_SIZE * RPKTS_PER_BLOCK;
	uint64_t size_full = N_BLOCKS_IN * size; 
    
	// Init GPU Mem 
	GPU_Init();
    GPU_GetDevInfo();
    // malloc memory on gpu0
    GPU_SetDevice(0);
    GPU_MallocBuffer((void **)&gpu_buf0, size_full);
	// malloc memory on gpu1
    GPU_SetDevice(1);
    GPU_MallocBuffer((void **)&gpu_buf1, size_full);
    Host_MallocBuffer((void**)&host_buf, size);
    // create stream
    GPU_CreateStream();
	// Pin Mem
	for(int i = 0; i< db_in->header.n_block; i++)
		Host_PinMem(&db_in->block[i], size);
    
	while(run_threads()){
        hashpipe_status_lock_safe(st);
        {
            hputi4(st->buf, "GPUBLKIN", curblock_in);
            hputs(st->buf, status_key, "waiting");
            hputi4(st->buf, "GPUBKOUT", curblock_out);
            hputi8(st->buf,"GPUMCNT",cur_mcnt);
            hputu8(st->buf,"TPKTLOSS",pkt_loss);
            hputnr8(st->buf, "GPUGBPS", 6, gbps);
			hputu8(st->buf,"RPKTLOSS",r_pkt_loss);
		}
        hashpipe_status_unlock_safe(st);
        
		// Wait for new input block to be filled
        while ((rv=input_databuf_wait_filled(db_in, curblock_in)) != HASHPIPE_OK) {
            if (rv==HASHPIPE_TIMEOUT) {
                hashpipe_status_lock_safe(st);
                hputs(st->buf, status_key, "blocked");
                hashpipe_status_unlock_safe(st);
                continue;
            } else {
                hashpipe_error(__FUNCTION__, "error waiting for filled databuf");
                pthread_exit(NULL);
                break;
            }
        }
		
        // Wait for new output block to be free
        while ((rv=output_databuf_wait_free(db_out, curblock_out)) != HASHPIPE_OK) {
            if (rv==HASHPIPE_TIMEOUT) {
                hashpipe_status_lock_safe(st);
                hputs(st->buf, status_key, "blocked compute out");
                hashpipe_status_unlock_safe(st);
                continue;
            } else {
                hashpipe_error(__FUNCTION__, "error waiting for free databuf");
                pthread_exit(NULL);
                break;
            }
        }

		//clock_gettime(CLOCK_MONOTONIC_RAW, &ts_start);
        
		// we only look at the mcnt here for checking packet loss.
        //TODO: move data into GPU for further processing
        for(slot_id = 0; slot_id<RPKTS_PER_BLOCK; slot_id++)
        {      
            cur_mcnt = *(uint64_t*)(db_in->block[curblock_in].adc_pkt[slot_id].pkt_header.mcnt);
            if(first_pkt == 0)
            {
                pre_mcnt = cur_mcnt;
                first_pkt = 1;
            }
			/*
            if(pre_mcnt != cur_mcnt)
            {
                printf("slot_id:%d\n", slot_id);
                printf("cur_mcnt: %ld; pre_mcnt: %ld\n", cur_mcnt, pre_mcnt);
            }
			*/
			if(pre_mcnt > cur_mcnt) 
		        cur_mcnt += CNT;
            r_pkt_loss = cur_mcnt - pre_mcnt; // this is the real-time pkt loss.
			pkt_loss +=  cur_mcnt - pre_mcnt; // this is total pkt loss.
			
			//if(pkt_loss > 0)
            /*
            if((cur_mcnt - pre_mcnt) > 0)
			{
				//pkt_loss = 0;
				printf("cur_mcnt = %ld, pre_mcnt = %ld\n", cur_mcnt, pre_mcnt);
			}
			*/	
			pre_mcnt = (cur_mcnt + 1)%CNT;
            
        }
		// measure the copy time
		clock_gettime(CLOCK_MONOTONIC_RAW, &ts_start);
		//gbps = GPU_MoveDataFromHost((void*)(&db_in->block[curblock_in]), gpu_buf+curblock_in*size, size);
       	//gbps = GPU_MoveDataFromHost(host_buf, gpu_buf+curblock_in*size, size);
        //GPU_MoveDataFromHost((void*)(&db_in->block[curblock_in]), gpu_buf0+curblock_in*size, size);
		GPU_MoveDataFromHostAsync((void*)(&db_in->block[curblock_in]), gpu_buf0+curblock_in*size, size/2,0);
        GPU_MoveDataFromHostAsync((void*)(&db_in->block[curblock_in]), gpu_buf1+curblock_in*size+size/2, size/2,1);
        GPU_StreamSync(0);
        GPU_StreamSync(1);
        clock_gettime(CLOCK_MONOTONIC_RAW, &ts_now);
		// Mark output block as filled 
        // TODO: move the processed data into output block
        output_databuf_set_filled(db_out, curblock_out);
        curblock_out = (curblock_out + 1) % db_out->header.n_block;
        // Mark input block as free and advance
        input_databuf_set_free(db_in, curblock_in);
        curblock_in = (curblock_in + 1) % db_in->header.n_block;
		//clock_gettime(CLOCK_MONOTONIC_RAW, &ts_now);
        ns_elapsed = ELAPSED_NS(ts_start, ts_now); 
        gbps = size *1.0 / ns_elapsed;
        //gbps = 0.0;
		// Check for cancel
        pthread_testcancel();
    }
}

static hashpipe_thread_desc_t gpu_thread = {
    name: "gpu_thread",
    skey: "GPUSTAT",
    init: init,
    run:  run,
    ibuf_desc: {input_databuf_create},
    obuf_desc: {output_databuf_create}
};

static __attribute__((constructor)) void ctor()
{
  register_hashpipe_thread(&gpu_thread);
}
