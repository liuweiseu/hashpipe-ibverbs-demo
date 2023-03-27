/*
 * ibverbs_pkt_recv_thread.c
 *
 * Routine to write pkt data from ibverbs into shared memory blocks.
 * The code is referred from SETI Institute: 
 *         https://github.com/MydonSolutions/hpguppi_daq/blob/seti-ata-8bit/src/hpguppi_ibverbs_pkt_thread.c
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

#define DEFAULT_MAX_FLOWS (16)

// The ibverbs_init() function sets up the hashpipe_ibv_context
// structure and then call hashpipe_ibv_init().  This uses the "user-managed
// buffers" feature of hashpipe_ibverbs so that packets will be stored directly
// into the data blocks of the shared memory databuf.  It initializes receive
// scatter/gather lists to point to slots in the first data block of the shared
// memory databuf.  Due to how the current hashpipe versions works, the virtual
// address mappings for the shared memory datbuf change between ini() and
// run(), so this function must be called from run() only.  It initializes
// receive scatter/gather lists to point to slots in the first data block of
// the shared memory databuf.  Returns HASHPIPE_OK on success, other values on
// error.
static int ibverbs_init(struct hashpipe_ibv_context * hibv_ctx,
                     hashpipe_status_t * st,
                     input_databuf_t * db)
{
    int i, j;
    struct hpguppi_pktbuf_info * pktbuf_info = hpguppi_pktbuf_info_ptr(db);
    uint32_t num_chunks = pktbuf_info->num_chunks;
    struct hpguppi_pktbuf_chunk * chunks = pktbuf_info->chunks;
    uint64_t base_addr;

    memset(hibv_ctx, 0, sizeof(struct hashpipe_ibv_context));

    // MAXFLOWS got initialized by init() if needed, but we setup the default
    // value again just in case some (buggy) downstream thread removed it from
    // the status buffer.
    hibv_ctx->max_flows = DEFAULT_MAX_FLOWS;

    hashpipe_status_lock_safe(st);
    {
        // Get info from status buffer if present (no change if not present)
        hgets(st->buf,  "IBVIFACE", sizeof(hibv_ctx->interface_name), hibv_ctx->interface_name);
        if(hibv_ctx->interface_name[0] == '\0') {
            hgets(st->buf,  "BINDHOST", sizeof(hibv_ctx->interface_name), hibv_ctx->interface_name);
            if(hibv_ctx->interface_name[0] == '\0') {
                strcpy(hibv_ctx->interface_name, "eth4");
                hputs(st->buf, "IBVIFACE", hibv_ctx->interface_name);
            }
        }

        hgetu4(st->buf, "MAXFLOWS", &hibv_ctx->max_flows);
        if(hibv_ctx->max_flows == 0) {
            hibv_ctx->max_flows = 1;
        }
        hputu4(st->buf, "MAXFLOWS", hibv_ctx->max_flows);
    }
    hashpipe_status_unlock_safe(st);

    // General fields
    hibv_ctx->nqp = 1;
    hibv_ctx->pkt_size_max = pktbuf_info->slot_size; // max for both send and receive //not pkt_size as it is used for the cumulative sge buffers
    hibv_ctx->user_managed_flag = 1;

    // Number of send/recv packets (i.e. number of send/recv WRs)
    hibv_ctx->send_pkt_num = 1;
    int num_recv_wr = hpguppi_query_max_wr(hibv_ctx->interface_name);
    hashpipe_info(__FUNCTION__, "max work requests of %s = %d", hibv_ctx->interface_name, num_recv_wr);
    if(num_recv_wr > pktbuf_info->slots_per_block) {
        num_recv_wr = pktbuf_info->slots_per_block;
    }
    hibv_ctx->recv_pkt_num = num_recv_wr;
    hashpipe_info(__FUNCTION__, "using %d work requests", num_recv_wr);

    if(hibv_ctx->recv_pkt_num * hibv_ctx->pkt_size_max > BLOCK_DATA_SIZE){
        // Should never happen
        hashpipe_warn(__FUNCTION__, "hibv_ctx->recv_pkt_num (%u)*(%u) hibv_ctx->pkt_size_max (%u) > (%lu) BLOCK_DATA_SIZE",
        hibv_ctx->recv_pkt_num, hibv_ctx->pkt_size_max, hibv_ctx->recv_pkt_num * hibv_ctx->pkt_size_max, BLOCK_DATA_SIZE);
    }

    // Allocate packet buffers
    if(!(hibv_ctx->send_pkt_buf = (struct hashpipe_ibv_send_pkt *)calloc(
        hibv_ctx->send_pkt_num, sizeof(struct hashpipe_ibv_send_pkt)))) {
        return HASHPIPE_ERR_SYS;
    }
    if(!(hibv_ctx->recv_pkt_buf = (struct hashpipe_ibv_recv_pkt *)calloc(
        hibv_ctx->recv_pkt_num, sizeof(struct hashpipe_ibv_recv_pkt)))) {
        return HASHPIPE_ERR_SYS;
    }

    // Allocate sge buffers.  We allocate num_chunks SGEs per receive WR.
    if(!(hibv_ctx->send_sge_buf = (struct ibv_sge *)calloc(
        hibv_ctx->send_pkt_num, sizeof(struct ibv_sge)))) {
        return HASHPIPE_ERR_SYS;
    }
    if(!(hibv_ctx->recv_sge_buf = (struct ibv_sge *)calloc(
        hibv_ctx->recv_pkt_num * num_chunks, sizeof(struct ibv_sge)))) {
        return HASHPIPE_ERR_SYS;
    }

    // Specify size of send and recv memory regions.
    // Send memory region is just one packet.  Recv memory region spans a data block, with
    // one recv memory region registered per block (see recv_mr_num).
    hibv_ctx->send_mr_size = (size_t)hibv_ctx->send_pkt_num * hibv_ctx->pkt_size_max;
    hibv_ctx->recv_mr_size = db->header.n_block * db->header.block_size;

    // Allocate memory for send_mr_buf
    if(!(hibv_ctx->send_mr_buf = (uint8_t *)calloc(
        hibv_ctx->send_pkt_num, hibv_ctx->pkt_size_max))) {
        return HASHPIPE_ERR_SYS;
    }
    // Point recv_mr_buf to starts of block 0
    hibv_ctx->recv_mr_buf = (uint8_t *)db->block;

    // Setup send WR's num_sge and SGEs' addr/length fields
    hibv_ctx->send_pkt_buf[0].wr.num_sge = 1;
    hibv_ctx->send_pkt_buf[0].wr.sg_list = hibv_ctx->send_sge_buf;
    hibv_ctx->send_sge_buf[0].addr = (uint64_t)hibv_ctx->send_mr_buf;
    hibv_ctx->send_sge_buf[0].length = hibv_ctx->pkt_size_max;

    // Setup recv WRs' num_sge and SGEs' addr/length fields
    for(i=0; i<hibv_ctx->recv_pkt_num; i++) {
        hibv_ctx->recv_pkt_buf[i].wr.wr_id = i;
        hibv_ctx->recv_pkt_buf[i].wr.num_sge = num_chunks;
        hibv_ctx->recv_pkt_buf[i].wr.sg_list = &hibv_ctx->recv_sge_buf[num_chunks*i];

        base_addr = (uint64_t)hpguppi_pktbuf_block_slot_ptr(db, 0, i);
        for(j=0; j<num_chunks; j++) {
        hibv_ctx->recv_sge_buf[num_chunks*i+j].addr = base_addr + chunks[j].chunk_offset;
        hibv_ctx->recv_sge_buf[num_chunks*i+j].length = chunks[j].chunk_size;
        }
    }

    // Initialize ibverbs
    return hashpipe_ibv_init(hibv_ctx);
}

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
