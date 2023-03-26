/*
 * ibverbs_pkt_recv_thread.c
 *
 * Routine to write pkt data from ibverbs into shared memory blocks.
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

static hashpipe_thread_desc_t ibverbs_pkt_recv_thread = {
    name: "ibverbs_pkt_recv_thread",
    skey: "NETSTAT",
    init: init,
    run:  run,
    ibuf_desc: {NULL},
    obuf_desc: {input_databuf_create}
};

static __attribute__((constructor)) void ctor()
{
  register_hashpipe_thread(&ibverbs_pkt_recv_thread);
}
