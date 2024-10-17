#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <sys/time.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <getopt.h>
#include <infiniband/verbs.h>

#include "ibv_utils.h"
#include "pkt_gen.h"
#include "utils.h"

uint64_t ns_elapsed;
struct timespec ts_start;
struct timespec ts_now;

/*
Print out help information.
*/
void print_send_helper()
{
    printf("Usage:\n");
    printf("    SenderSim   Sender Simulator\n\n");
    printf("Options:\n");
    printf("    -d, NIC dev number. '0' means mlx5_0.\n");
    printf("    -n, the packet number in one group. By default, n = 512.\n");
    printf("    -N, the number of packet groups. By default, N = 1.\n");
    printf("    --smac, source MAC address.\n");
    printf("    --dmac, destination MAC address.\n");
    printf("    --sip, source IP address.\n");
    printf("    --dip, destination IP address.\n");
    printf("    --sport, source port number.\n");
    printf("    --dport, destination port number.\n");
    printf("    --streams, number of streams.\n");
    printf("    --inf, keep sending packets.\n");
    printf("    --npkt_row. the number of the same packets sent out.\n");
    printf("                By default, npkt_row = 1, which means different packets are sent out one by one.\n");  
    printf("    --help, -h,  print out the helper information.\n");
}

int main(int argc, char *argv[])
{
    int num_dev = 0, wr_num = 0;
    struct send_args args;
    init_send_args(&args);
    parse_send_args(&args, argc, argv);
    if(args.help_info)
    {
        print_send_helper();
        return 0;
    }
    print_send_info(&args);

    struct ibv_utils_res ibv_res;
    memset(&ibv_res, 0, sizeof(struct ibv_utils_res));
    // get values from args
    wr_num = args.npkt_per_grp;
    ibv_res.recv_nsge = 1;
    ibv_res.send_nsge = 1;
    printf("Start to send...\n");

    // get ib device list
    num_dev = get_ib_devices();
    printf("The number of ib devices is %d.\n", num_dev);

    // open ib device by id
    int ret = 0;
    ret = open_ib_device(args.device_id, &ibv_res);
    if (ret < 0) {
        printf("Failed to open IB device.\n");
        return -1;
    }
    printf("Open IB device successfully.\n");

    // only implement recv here
    ret = create_ib_res(&ibv_res, wr_num, 0);
    if (ret < 0) {
        printf("Failed to create ib resources.\n");
        return -2;
    }
    else
    {
        printf("Create IB resources successfully.\n");
    }

    // init ib resources
    ret = init_ib_res(&ibv_res);
    if (ret < 0) {
        printf("Failed to init ib resources.\n");
        return -3;
    }
    else
    {
        printf("Init IB resources successfully.\n");
    }

    // create pkts
    void *buf;
    uint32_t buf_size = PKT_LEN * wr_num * ibv_res.send_nsge;
    buf = malloc(buf_size);
    if (buf == NULL) {
        printf("Failed to allocate memory.\n");
        return -4;
    }
    // generate pkts
    int i = 0;
    while(i < wr_num * ibv_res.send_nsge)
    {
        for(int j = 0; j < args.streams; j++)
        {   
            for(int k = 0; k < args.npkt_row; k++)
            {
                struct udp_pkt *pkt = (struct udp_pkt *)((uint8_t *)buf + i * PKT_LEN);
                set_dest_mac(pkt, args.pkt_info[j].dst_mac);
                set_src_mac(pkt, args.pkt_info[j].src_mac);
                set_eth_type(pkt, (uint8_t *)"\x08\x00");
                set_src_ip(pkt, (uint8_t *)(&args.pkt_info[j].src_ip));
                set_dst_ip(pkt, (uint8_t *)(&args.pkt_info[j].dst_ip));
                set_udp_src_port(pkt, args.pkt_info[j].src_port);
                set_udp_dst_port(pkt, args.pkt_info[j].dst_port);
                set_pkt_len(pkt, PKT_LEN - 34);
                set_payload(pkt, (uint8_t *)"Hello, world!", 13);
                i++;
                if(i >= wr_num * ibv_res.send_nsge)break;
            }
        }
    }

    // register memory
    ret = register_memory(&ibv_res, buf, buf_size, PKT_LEN);
    if (ret < 0) {
        printf("Failed to register memory.\n");
        return -5;
    }
    else
    {
        printf("Register memory successfully.\n");
    }

    // send pkts
    i = 0;
    while((i < args.npkt_grp) || args.inf) 
    {
        ret = ib_send(&ibv_res);
        if (ret < 0) {
            printf("Failed to send pkts.\n");
            return -6;
        }
        i++;
    }
    printf("Send pkts successfully.\n");
    free(buf);
    free_send_args(&args);
    destroy_ib_res(&ibv_res);
    close_ib_device(&ibv_res);
    return 0;
}

