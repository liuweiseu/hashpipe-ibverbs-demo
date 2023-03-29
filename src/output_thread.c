/*
 * output_thread.c
 *
 * Routine to write processed data to disk.
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

// Initialization function for Hashpipe.
// This function is called once when the thread is created
// args: Arugments passed in by hashpipe framework.
static int init(hashpipe_thread_args_t * args)
{
    hashpipe_status_t st = args->st;
    const char * status_key = args->thread_desc->skey;

    return 0;
}

static void *run(hashpipe_thread_args_t * args)
{
    output_databuf_t *db = (output_databuf_t *)args->ibuf;
    hashpipe_status_t * st = &args->st;
    const char * thread_name = args->thread_desc->name;
    const char * status_key = args->thread_desc->skey;

    int rv = 0;
    int curblock_in=0;

    // Now, we just check if we have some filled output block.
    // TODO: Do something on the data in the output block.
    while(run_threads()){
        hashpipe_status_lock_safe(st);
        {
            hputi4(st->buf, "OUTBLKIN", curblock_in);
            hputs(st->buf, status_key, "waiting");
        }
        hashpipe_status_unlock_safe(st);

    // Wait for new input block to be filled
    while ((rv=input_databuf_wait_free(db, curblock_in)) != HASHPIPE_OK) {
        if (rv==HASHPIPE_TIMEOUT) {
            hashpipe_status_lock_safe(&st);
            hputs(st->buf, status_key, "blocked");
            hashpipe_status_unlock_safe(&st);
            continue;
        } else {
            hashpipe_error(__FUNCTION__, "error waiting for filled databuf");
            pthread_exit(NULL);
            break;
        }
    }

    // TODO: Do something on the data here!

    // Mark input block as free and advance
    input_databuf_set_free(db, curblock_in);
    curblock_in = (curblock_in + 1) % db->header.n_block;
        
    // Check for cancel
    pthread_testcancel(); 
    }
}

static hashpipe_thread_desc_t output_thread = {
    name: "output_thread",
    skey: "OUTSTAT",
    init: init,
    run:  run,
    ibuf_desc: {output_databuf_create},
    obuf_desc: {NULL}
};

static __attribute__((constructor)) void ctor()
{
  register_hashpipe_thread(&output_thread);
}
