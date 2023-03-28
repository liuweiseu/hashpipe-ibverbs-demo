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
#include "hashpipe_ibverbs.h"
#include "databuf.h"

#include <linux/time.h>

// Milliseconds between periodic status buffer updates
#define PERIODIC_STATUS_BUFFER_UPDATE_MS (200)

#define DEFAULT_MAX_FLOWS (16)

#ifndef ELAPSED_NS
#define ELAPSED_NS(start,stop) \
  (ELAPSED_S(start,stop)*1000*1000*1000+((stop).tv_nsec-(start).tv_nsec))
#endif

// Function to get a pointer to slot "slot_id" in block "block_id" of databuf
// "db".
static inline uint8_t *pktbuf_block_slot_ptr(input_databuf_t *db,
                                             uint64_t block_id, uint32_t slot_id)
{
  struct hpguppi_pktbuf_info * pktbuf_info = hpguppi_pktbuf_info_ptr(db);
  block_id %= db->header.n_block;
  return (uint8_t *)db->block[block_id].adc_pkt + slot_id * RPKT_SIZE;
}

// Function to get a pointer to a databuf's hashpipe_ibv_context structure.
// Assumes that the hashpipe_ibv_context  structure is tucked into the
// "padding" bytes of the hpguppi_intput_databuf just after the pktbuf_info
// structure.
// TODO This is all too ugly!  Create a new databuf type!!!
// TODO Check db->header.data_type?
static inline struct hashpipe_ibv_context *hashpipe_ibv_context_ptr(input_databuf_t *db)
{
  return (struct hashpipe_ibv_context *)(db->padding);
}

// Wait for a block_info's databuf block to be free, then copy status buffer to
// block's header and clear block's data.  Calling thread will exit on error
// (should "never" happen).  Status buffer updates made after the copy to the
// block's header will not be seen in the block's header (e.g. by downstream
// threads).  Any status buffer fields that need to be updated for correct
// downstream processing of this block must be updated BEFORE calling this
// function.  Note that some of the block's header fields will be set when the
// block is finalized (see finalize_block() for details).
static void wait_for_block_free(input_databuf_t *db, int block_idx,
    hashpipe_status_t * st, const char * status_key)
{
  int rv;
  char ibvstat[80] = {0};
  char ibvbuf_status[80];
  int ibvbuf_full = hashpipe_databuf_total_status(db);
  sprintf(ibvbuf_status, "%d/%d", ibvbuf_full, db->header.n_block);

  hashpipe_status_lock_safe(st);
  {
    // Save original status
    hgets(st->buf, status_key, sizeof(ibvstat), ibvstat);
    // Set "waitfree" status
    hputs(st->buf, status_key, "waitfree");
    // Update IBVBUFST
    hputs(st->buf, "IBVBUFST", ibvbuf_status);
  }
  hashpipe_status_unlock_safe(st);

  while ((rv=hashpipe_databuf_wait_free(db, block_idx))
      != HASHPIPE_OK) {
    if (rv==HASHPIPE_TIMEOUT) {
      ibvbuf_full = hashpipe_databuf_total_status(db);
      sprintf(ibvbuf_status, "%d/%d", ibvbuf_full, db->header.n_block);
      hashpipe_status_lock_safe(st);
      {
        hputs(st->buf, status_key, "blocked");
        hputs(st->buf, "IBVBUFST", ibvbuf_status);
      }
      hashpipe_status_unlock_safe(st);
    } else {
      hashpipe_error(__FUNCTION__,
          "error waiting for free databuf (%s)", __FILE__);
      pthread_exit(NULL);
    }
  }
  hashpipe_status_lock_safe(st);
  {
    // Restore original status
    hputs(st->buf, status_key, ibvstat);
  }
  hashpipe_status_unlock_safe(st);
}
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
    int i = 0;
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
    hibv_ctx->pkt_size_max = RPKT_SIZE; // max for both send and receive //not pkt_size as it is used for the cumulative sge buffers
    hibv_ctx->user_managed_flag = 1;

    // Number of send/recv packets (i.e. number of send/recv WRs)
    hibv_ctx->send_pkt_num = 1;
    int num_recv_wr = hpguppi_query_max_wr(hibv_ctx->interface_name);
    hashpipe_info(__FUNCTION__, "max work requests of %s = %d", hibv_ctx->interface_name, num_recv_wr);
    if(num_recv_wr > RPKTS_PER_BLOCK) {
        num_recv_wr = RPKTS_PER_BLOCK;
    }
    hibv_ctx->recv_pkt_num = num_recv_wr;
    hashpipe_info(__FUNCTION__, "using %d work requests", num_recv_wr);

    if(hibv_ctx->recv_pkt_num * hibv_ctx->pkt_size_max > BLOCK_IN_DATA_SIZE){
        // Should never happen
        hashpipe_warn(__FUNCTION__, "hibv_ctx->recv_pkt_num (%u)*(%u) hibv_ctx->pkt_size_max (%u) > (%lu) BLOCK_IN_DATA_SIZE",
        hibv_ctx->recv_pkt_num, hibv_ctx->pkt_size_max, hibv_ctx->recv_pkt_num * hibv_ctx->pkt_size_max, BLOCK_IN_DATA_SIZE);
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

    // Allocate sge buffers.  We allocate 1 SGE per receive WR.
    if(!(hibv_ctx->send_sge_buf = (struct ibv_sge *)calloc(
        hibv_ctx->send_pkt_num, sizeof(struct ibv_sge)))) {
        return HASHPIPE_ERR_SYS;
    }
    if(!(hibv_ctx->recv_sge_buf = (struct ibv_sge *)calloc(
        hibv_ctx->recv_pkt_num, sizeof(struct ibv_sge)))) {
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
        hibv_ctx->recv_pkt_buf[i].wr.num_sge = 1;
        hibv_ctx->recv_pkt_buf[i].wr.sg_list = &hibv_ctx->recv_sge_buf[i];

        base_addr = (uint64_t)pktbuf_block_slot_ptr(db, 0, i);
        hibv_ctx->recv_sge_buf[i].addr = base_addr + i  * RPKT_SIZE;
        hibv_ctx->recv_sge_buf[i].length = RPKT_SIZE;
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
    strcpy(ibvpktsz, "8256");

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
    input_databuf_t *db = (input_databuf_t *)args->obuf;
    hashpipe_status_t * st = &args->st;
    const char * thread_name = args->thread_desc->name;
    const char * status_key = args->thread_desc->skey;

    // The all important hashpipe_ibv_context
    struct hashpipe_ibv_context * hibv_ctx = hashpipe_ibv_context_ptr(db);

    // Variables for handing received packets
    struct hashpipe_ibv_recv_pkt * hibv_rpkt = NULL;
    struct hashpipe_ibv_recv_pkt * curr_rpkt;

    // Misc counters, etc
    int i;
    uint64_t base_addr;
    int got_wc_error = 0;
    int rv = 0;
    uint64_t curblk = 0;
    uint64_t next_block = 0;
    uint32_t next_slot = 0;

    wait_for_block_free(db, curblk % N_BLOCKS_IN, st, status_key);
    // Initialize IBV
    if(hpguppi_ibverbs_init(hibv_ctx, st, db)) {
        hashpipe_error(thread_name, "ibverbs_init failed");
        return NULL;
    }
    
    // Initialize next slot
    next_slot = hibv_ctx->recv_pkt_num + 1;
    if(next_slot > RPKTS_PER_BLOCK) {
        next_slot = 0;
        next_block++;
    }
    // Variables for counting packets and bytes as well as elapsed time
    uint64_t bytes_received = 0;
    uint64_t pkts_received = 0;
    struct timespec ts_start;
    struct timespec ts_now;
    uint64_t ns_elapsed;
    double gbps;
    double pps;

    while (run_threads()) {
        hibv_rpkt = hashpipe_ibv_recv_pkts(hibv_ctx, 50); // 50 ms timeout

        // If no packets and errno is non-zero
        if(!hibv_rpkt && errno) {
        // Print error, reset errno, and continue receiving
        hashpipe_error(thread_name, "hashpipe_ibv_recv_pkts");
        errno = 0;
        continue;
        }

        // Check for periodic status buffer update interval
        clock_gettime(CLOCK_MONOTONIC_RAW, &ts_now);
        ns_elapsed = ELAPSED_NS(ts_start, ts_now);
        if(ns_elapsed >= PERIODIC_STATUS_BUFFER_UPDATE_MS*1000*1000) {
            // Save now as the new start
            ts_start = ts_now;

            // Calculate stats
            gbps = 8.0 * bytes_received / ns_elapsed;
            pps = 1e9 * pkts_received / ns_elapsed;
            // Update status buffer fields
            hashpipe_status_lock_safe(st);
            {
                hputnr8(st->buf, "IBVGBPS", 6, gbps);
                hputnr8(st->buf, "IBVPPS", 3, pps);
            }
            hashpipe_status_unlock_safe(st);
            // Reset counters
            bytes_received = 0;
            pkts_received = 0;
        }
        // If no packets
        if(!hibv_rpkt) {
            // Wait for more packets
            continue;
        }

        // Got packets!

        // For each packet: update SGE addr
        for(curr_rpkt = hibv_rpkt; curr_rpkt;
            curr_rpkt = (struct hashpipe_ibv_recv_pkt *)curr_rpkt->wr.next) {

        if(curr_rpkt->length == 0) {
            hashpipe_error(thread_name,
                "WR %d got error when using address: %p (databuf %p +%lu)",
                curr_rpkt->wr.wr_id,
                curr_rpkt->wr.sg_list->addr,
                db->block, sizeof(db->block));
            // Set flag to break out of main loop and then break out of for loop
            got_wc_error = 1;
            break;
        }
        // If time to advance the ring buffer block
        if(next_block > curblk+1) {
            // Mark curblk as filled
            hashpipe_databuf_set_filled(db, curblk % N_BLOCKS_IN);

            // Increment curblk
            curblk++;

            // Wait for curblk+1 to be free
            wait_for_block_free(db, (curblk+1) % N_BLOCKS_IN, st, status_key);
        } // end block advance

        // Count packet and bytes
        pkts_received++;
        bytes_received += curr_rpkt->length;

        // Update current WR with new destination addresses for all SGEs
        base_addr = (uint64_t)pktbuf_block_slot_ptr(db, next_block, next_slot);
        curr_rpkt->wr.sg_list->addr = base_addr;
        
        // Advance slot
        next_slot++;
        if(next_slot >= RPKTS_PER_BLOCK) {
            next_slot = 0;
            next_block++;
        }
    }
    // Break out of main loop if we got a work completion error
    if(got_wc_error) {
      break;
    }

    // Release packets (i.e. repost work requests)
    if(hashpipe_ibv_release_pkts(hibv_ctx,
          (struct hashpipe_ibv_recv_pkt *)hibv_rpkt)) {
      hashpipe_error(thread_name, "hashpipe_ibv_release_pkts");
      errno = 0;
    }

    // Will exit if thread has been cancelled
    pthread_testcancel();
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
