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

    input_databuf_t *db = (input_databuf_t *)args->obuf;
    hashpipe_status_t * st = &args->st;
    const char * status_key = args->thread_desc->skey;

    // Variables to get/set status buffer fields
    uint32_t max_flows = DEFAULT_MAX_FLOWS;
    char ifname[80] = {0};
    char ibvpktsz[80];
    strcpy(ibvpktsz, "8192");

    hashpipe_status_lock_safe(st);
    {
        // Get info from status buffer if present (no change if not present)
        hgets(st->buf,  "IBVIFACE", sizeof(ifname), ifname);
        if(ifname[0] == '\0') {
        hgets(st->buf,  "BINDHOST", sizeof(ifname), ifname);
        if(ifname[0] == '\0') {
            strcpy(ifname, "eth4");
            hputs(st->buf, "IBVIFACE", ifname);
        }
        }

        hgets(st->buf,  "IBVPKTSZ", sizeof(ibvpktsz), ibvpktsz);
        hgetu4(st->buf, "MAXFLOWS", &max_flows);

        if(max_flows == 0) {
        max_flows = 1;
        }
        // Store ibvpktsz in status buffer (in case it was not there before).
        hputs(st->buf, "IBVPKTSZ", ibvpktsz);
        hputu4(st->buf, "MAXFLOWS", max_flows);
        // Set status_key to init
        hputs(st->buf, status_key, "init");
    }
    hashpipe_status_unlock_safe(st);

    // Success!
    return HASHPIPE_OK;

}

static void *run(hashpipe_thread_args_t * args)
{
    // Local aliases to shorten access to args fields
    hpguppi_input_databuf_t *db = (hpguppi_input_databuf_t *)args->obuf;
    hashpipe_status_t * st = &args->st;
    const char * thread_name = args->thread_desc->name;
    const char * status_key = args->thread_desc->skey;
    
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
