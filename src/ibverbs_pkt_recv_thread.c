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

#include <infiniband/verbs.h>

#include "hashpipe.h"
#include "hashpipe_ibverbs.h"
#include "databuf.h"


#include <time.h>

#define DEST_MAC {0xb8, 0xce, 0xf6, 0xe5, 0x6b, 0x5a}
#define SRC_MAC {0x0c, 0x42, 0xa1, 0xbe, 0x34, 0xf8}

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
  block_id %= db->header.n_block;
  return (uint8_t *)db->block[block_id].adc_pkt + slot_id * RPKT_SIZE;
}

// Queries the device specified by interface_name and returns max_qp_wr, or -1
// on error.
static int query_max_wr(const char * interface_name)
{
  uint64_t interface_id;
  struct ibv_device_attr* ibv_dev_attr = malloc(sizeof(struct ibv_device_attr));
  struct ibv_context* ibv_context = NULL;
  int max_qp_wr = -1;
  if(hashpipe_ibv_get_interface_info(interface_name, NULL, &interface_id)) {
    hashpipe_error(interface_name, "error getting interace info");
    errno = 0;
    return -1;
  }
  if(hashpipe_ibv_open_device_for_interface_id(
        interface_id, &ibv_context, ibv_dev_attr, NULL)) {
    // Error message already logged
    return -1;
  }
  max_qp_wr = ibv_dev_attr->max_qp_wr;
  free(ibv_dev_attr);
  return max_qp_wr;
}

/*
// Create sniffer flow
// Use with caution!!!
static struct ibv_flow *create_sniffer_flow(struct hashpipe_ibv_context * hibv_ctx, uint16_t sniffer_flag)
{
  struct hashpipe_ibv_flow sniffer_flow = {
    .attr = {
      .comp_mask      = 0,
      .type           = IBV_FLOW_ATTR_NORMAL,
      .size           = sizeof(sniffer_flow.attr)
                        + sizeof(struct ibv_flow_spec_ipv4)
                        + sizeof(struct ibv_flow_spec_eth)
                        + sizeof(struct ibv_flow_spec_tcp_udp),
      .priority       = 0,
      .num_of_specs   = 0,
      .port           = hibv_ctx->port_num,
      .flags          = 0
    },
    .spec_eth = {
      .type   = IBV_FLOW_SPEC_ETH,
      .size   = sizeof(sniffer_flow.spec_eth),
    },
    .spec_ipv4 = {
      .type   = IBV_FLOW_SPEC_IPV4,
      .size   = sizeof(sniffer_flow.spec_ipv4),
    },
    .spec_tcp_udp = {
      .type   = IBV_FLOW_SPEC_UDP,
      .size   = sizeof(sniffer_flow.spec_tcp_udp),
      .val.dst_port = htobe16(sniffer_flag),
      .mask.dst_port = sniffer_flag >= 1024 ? 0xffff : 0x0,
    }
  };

  if(sniffer_flag > 1) {
    hashpipe_info(
      __FUNCTION__,
      "Masked sniffer ibv_flow to destination MAC %02X:%02X:%02X:%02X:%02X:%02X",
      hibv_ctx->mac[0],
      hibv_ctx->mac[1],
      hibv_ctx->mac[2],
      hibv_ctx->mac[3],
      hibv_ctx->mac[4],
      hibv_ctx->mac[5]
    );
    sniffer_flow.attr.num_of_specs = 1;
    memcpy(sniffer_flow.spec_eth.val.dst_mac, hibv_ctx->mac, 6);
    memset(sniffer_flow.spec_eth.mask.dst_mac, 0xff, 6);
  }
  if(sniffer_flag >= 49152) {
    hashpipe_info(__FUNCTION__, "Masked sniffer ibv_flow to ephemeral port %d (in range [49152, 65535]).", sniffer_flag);
    sniffer_flow.attr.num_of_specs = 3;
  }

  return ibv_create_flow(hibv_ctx->qp[0], (struct ibv_flow_attr*) &sniffer_flow.attr);
}

// Destroy sniffer flow
// Use with caution!!!
static int destroy_sniffer_flow(struct ibv_flow * sniffer_flow)
{
  return ibv_destroy_flow(sniffer_flow);
}
*/
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
  int ibvbuf_full = hashpipe_databuf_total_status((hashpipe_databuf_t *)db);
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

  while ((rv=input_databuf_wait_free(db, block_idx))!= HASHPIPE_OK) {
    if (rv==HASHPIPE_TIMEOUT) {
      ibvbuf_full = hashpipe_databuf_total_status((hashpipe_databuf_t *)db);
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
    hibv_ctx->port_num = 1;
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
    
    /*int num_recv_wr = query_max_wr(hibv_ctx->interface_name);
    hashpipe_info(__FUNCTION__, "max work requests of %s = %d", hibv_ctx->interface_name, num_recv_wr);
    if(num_recv_wr > RPKTS_PER_BLOCK) {
        num_recv_wr = RPKTS_PER_BLOCK;
    }
    */
    int num_recv_wr = RPKTS_PER_BLOCK;
    //int num_recv_wr = RPKTS_PER_BLOCK;
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
        hibv_ctx->recv_sge_buf[i].addr = base_addr;
        hibv_ctx->recv_sge_buf[i].length = hibv_ctx->pkt_size_max;
    }
    hashpipe_info(__FUNCTION__,"db->block: 0x%llx", (uint64_t)db->block);
    // Initialize ibverbs
    return hashpipe_ibv_init(hibv_ctx);
}

// Initialization function for Hashpipe.
// This function is called once when the thread is created
// args: Arugments passed in by hashpipe framework.
static int init(hashpipe_thread_args_t * args)
{
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
    //struct hashpipe_ibv_context hibv_ctx_inst;
    //struct hashpipe_ibv_context * hibv_ctx = &hibv_ctx_inst;
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
    struct ibv_flow * sniffer_flow = NULL;
    int32_t sniffer_flag = -1;


    wait_for_block_free(db, curblk % N_BLOCKS_IN, st, status_key);
    
    // Initialize IBV
    if(ibverbs_init(hibv_ctx, st, db)) {
        hashpipe_error(thread_name, "ibverbs_init failed");
        return NULL;
    }
    errno = 0;
    hashpipe_info(__FUNCTION__,"value of hibv_ctx->recv_cc: 0x%llx", *((uint64_t*)(hibv_ctx->recv_cc)));
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

    uint32_t flow_idx = 0;
    enum ibv_flow_spec_type flow_type = IBV_FLOW_SPEC_UDP;
    uint8_t src_mac[6] = {0x0c, 0x42, 0xa1, 0xbe, 0x34, 0xf8};
    uint16_t  ether_type = 0;
    uint16_t  vlan_tag = 0;
    uint32_t  src_ip = 0xc0a80202;
    uint32_t  dst_ip = 0xc0a80228;
    uint16_t  src_port = 49152;
    uint16_t  dst_port = 49152;
    
    /*
    hashpipe_ibv_flow( hibv_ctx, flow_idx, flow_type,
                       hibv_ctx->mac, src_mac,
                       ether_type,   vlan_tag,
                       src_ip,       dst_ip,
                       src_port,     dst_port);
    */
    struct raw_eth_flow_attr {
    struct ibv_flow_attr attr;
    struct ibv_flow_spec_eth spec_eth;
    } __attribute__((packed)) flow_attr = {
    .attr = {
      .comp_mask = 0,
      .type = IBV_FLOW_ATTR_NORMAL,
      .size = sizeof(flow_attr),
      .priority = 0,
      .num_of_specs = 1,
      .port = 1,
      .flags = 0,
    },
    .spec_eth = {
      //.type = IBV_EXP_FLOW_SPEC_ETH,
      .type = IBV_FLOW_SPEC_ETH,
      .size = sizeof(struct ibv_flow_spec_eth),
      //.size = sizeof (struct IBV_FLOW_SPEC_ETH),
      .val = {
        .dst_mac = DEST_MAC,
        .src_mac = SRC_MAC,
        .ether_type = 0,
        .src_mac = SRC_MAC,
        .vlan_tag = 0,
      },
      .mask = {
        .dst_mac = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
      .src_mac = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
      .ether_type = 0,
      .vlan_tag = 0,
      }
      }
    };
    struct ibv_flow *eth_flow;
    eth_flow = ibv_create_flow(hibv_ctx->qp[0], &flow_attr.attr);
    if (!eth_flow) {
      fprintf(stderr, "Couldn't attach steering flow\n");
      exit(1);
    }
    hashpipe_info(thread_name,"hibv_ctx->revc_cc=0x%lx\n",(unsigned long)hibv_ctx->recv_cc);  
    hashpipe_info(thread_name,"hibv_ctx->recv_cq=0x%lx\n",(unsigned long)hibv_ctx->recv_cq);                 
    // Update status_key with running state
    hashpipe_status_lock_safe(st);
    {
        hgeti4(st->buf, "IBVSNIFF", &sniffer_flag);
        hputs(st->buf, status_key, "running");
    }
    hashpipe_status_unlock_safe(st);

    hashpipe_info(__FUNCTION__, "db->padding: %llx", (uint64_t)db->padding);
    hashpipe_info(__FUNCTION__, "db->block: %llx", (uint64_t)db->block);
    hashpipe_info(__FUNCTION__, "db->block.adc_pkt: %llx", (uint64_t)db->block->adc_pkt);
    
    hashpipe_info(__FUNCTION__,"value of hibv_ctx->recv_cc: 0x%llx", *((uint64_t*)(hibv_ctx->recv_cc)));

    while (run_threads()) {
        hibv_rpkt = hashpipe_ibv_recv_pkts(hibv_ctx, 50); // 50 ms timeout

        // If no packets and errno is non-zero
        if(!hibv_rpkt && errno) {
            // Print error, reset errno, and continue receiving
            //hashpipe_error(thread_name, "hashpipe_ibv_recv_pkts");
            fprintf(stderr,"Error: %s %s\n",thread_name, "hashpipe_ibv_recv_pkts");
            fprintf(stderr,"Error: %s errno: %d\n",thread_name, errno);
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

            /*
            // Manage sniffer_flow as needed
            if(sniffer_flag > 0 && !sniffer_flow) {
                if(!(sniffer_flow = create_sniffer_flow(hibv_ctx, sniffer_flag))) {
                hashpipe_error(thread_name, "create_sniffer_flow failed");
                errno = 0;
                sniffer_flag = -1;
                } else {
                hashpipe_info(thread_name, "create_sniffer_flow succeeded");
                }
            } else if (sniffer_flag == 0 && sniffer_flow) {
                if(destroy_sniffer_flow(sniffer_flow)) {
                hashpipe_error(thread_name, "destroy_sniffer_flow failed");
                errno = 0;
                sniffer_flag = -1;
                } else {
                hashpipe_info(thread_name, "destroy_sniffer_flow succeeded");
                }
                sniffer_flow = NULL;
            }*/
            
        }
        // If no packets
        if(!hibv_rpkt) {
            // Wait for more packets
            continue;
        }

        // For each packet: update SGE addr
        for(curr_rpkt = hibv_rpkt; curr_rpkt;curr_rpkt = (struct hashpipe_ibv_recv_pkt *)curr_rpkt->wr.next) {

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
                input_databuf_set_filled(db, curblk % N_BLOCKS_IN);

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
        if(hashpipe_ibv_release_pkts(hibv_ctx,(struct hashpipe_ibv_recv_pkt *)hibv_rpkt)) {
            hashpipe_error(thread_name, "hashpipe_ibv_release_pkts");
            errno = 0;
        }

        // Will exit if thread has been cancelled
        pthread_testcancel();
        }
        return NULL;
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
