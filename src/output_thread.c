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

    return 0;
}

static void *run(hashpipe_thread_args_t * args)
{

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
