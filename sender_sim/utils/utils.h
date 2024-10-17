#ifndef __UTILS_H__
#define __UTILS_H__

#include<stdio.h>
#include <stdint.h>
#include <getopt.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <inttypes.h>

#define MEASURE_BANDWIDTH(size, t) ((double)size * 8.0 / t)

#define ELAPSED_NS(start,stop) \
    (((int64_t)stop.tv_sec-start.tv_sec)*1000*1000*1000+(stop.tv_nsec-start.tv_nsec))


struct pkt_info {
    uint8_t src_mac[6];
    uint8_t dst_mac[6];
    uint32_t src_ip;
    uint32_t dst_ip;
    uint16_t src_port;
    uint16_t dst_port;
};

struct send_args {
    uint8_t device_id;
    struct pkt_info *pkt_info;
    uint8_t help_info;
    uint8_t streams;
    uint32_t npkt_per_grp;
    uint32_t npkt_grp;
    uint32_t npkt_row;
    uint8_t inf;
};

void init_send_args(struct send_args *args);
void parse_send_args(struct send_args *args, int argc, char *argv[]);
void print_send_info(struct send_args *args);
void free_send_args(struct send_args *args);

#endif