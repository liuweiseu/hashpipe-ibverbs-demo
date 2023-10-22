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

// Initialization function for Hashpipe.
// This function is called once when the thread is created
// args: Arugments passed in by hashpipe framework.
static int init(hashpipe_thread_args_t * args)
{
    hashpipe_status_t st = args->st;
    const char * status_key = args->thread_desc->skey;
    hashpipe_status_lock_safe(&st);
	hputu8(st.buf,"PKTLOSS",0);
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

    int rv;
    int slot_id = 0;
    uint64_t cur_mcnt=0, pre_mcnt = 0;
    uint64_t pkt_loss = 0;
    int curblock_in=0;
    int curblock_out=0;
    int first_pkt = 0;

    double gbps = 0;

    void *gpu_buf;
    int size = RPKT_SIZE * RPKTS_PER_BLOCK;
    

    GPU_GetDevInfo();
    GPU_MallocBuffer((void **)&gpu_buf, size);

    while(run_threads()){
        hashpipe_status_lock_safe(st);
        {
            hputi4(st->buf, "GPUBLKIN", curblock_in);
            hputs(st->buf, status_key, "waiting");
            hputi4(st->buf, "GPUBKOUT", curblock_out);
            hputi8(st->buf,"GPUMCNT",cur_mcnt);
            hputu8(st->buf,"PKTLOSS",pkt_loss);
            hputnr8(st->buf, "GPUGBPS", 6, gbps);
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
                printf("cur_mcnt: %ld; pre_mcnt: %ld\n", cur_mcnt, pre_mcnt);
            }
			*/
            pkt_loss +=  cur_mcnt - pre_mcnt;
            pre_mcnt = (cur_mcnt + 1)%512;
        }
        // update status buffer
        /*
        hashpipe_status_lock_safe(st);
        {
	        hputu8(st->buf,"PKTLOSS",pkt_loss);
        }
        hashpipe_status_unlock_safe(st);
        */
        //gbps = GPU_MoveDataFromHost((void*)(&db_in->block[curblock_in]), gpu_buf, size);
        // Mark output block as filled 
        // TODO: move the processed data into output block
        output_databuf_set_filled(db_out, curblock_out);
        curblock_out = (curblock_out + 1) % db_out->header.n_block;
        // Mark input block as free and advance
        input_databuf_set_free(db_in, curblock_in);
        curblock_in = (curblock_in + 1) % db_in->header.n_block;
        
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
