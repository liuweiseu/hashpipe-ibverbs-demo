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

// Initialization function for Hashpipe.
// This function is called once when the thread is created
// args: Arugments passed in by hashpipe framework.
static int init(hashpipe_thread_args_t * args)
{

    return 0;
}

static void *run(hashpipe_thread_args_t * args)
{

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
