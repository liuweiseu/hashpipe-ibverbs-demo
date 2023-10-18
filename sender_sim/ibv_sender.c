#include <infiniband/verbs.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
 
#define PKT_LEN 8256

struct packet{
    unsigned char dst_mac[6];
    unsigned char src_mac[6];
    unsigned char eth_type[2];
    unsigned char ip_hdrs[12];
    unsigned char src_ip[4];
    unsigned char dst_ip[4];
    unsigned char udp_hdr[8];
    unsigned char mcnt[8];
    unsigned char payload[PKT_LEN - 50];
};

#define PORT_NUM 1
#define SQ_NUM_DESC 512 /* maximum number of sends waiting for completion */

/* template of packet to send - in this case icmp */

#define SRC_MAC 0xa0, 0x88, 0xc2, 0x0d, 0x5e, 0x28 //6

#define DST_MAC 0x94, 0x6d, 0xae, 0xac, 0xf8, 0x38 // 6

#define ETH_TYPE 0x08, 0x00 // 2

#define IP_HDRS 0x45, 0x00, 0x1f, 0x54, 0x00, 0x00, 0x40, 0x00, 0x40, 0x11, 0xaf, 0xb6 //12

#define SRC_IP 0xc0, 0xa8, 0x03, 0x02 // 4

#define DST_IP 0xc0, 0xa8, 0x03, 0x0c // 4

#define UDP_HDR 0xc0, 0x00, 0xc0, 0x00, 0x1f, 0x40, 0x00, 0x00 // 8

char packet[] = {

DST_MAC , SRC_MAC, ETH_TYPE, IP_HDRS, SRC_IP, DST_IP, UDP_HDR

};

#define DEV_NO  0
#define SG_N    1
#define QP_N    1

int main() {

    printf("Start...\n");
    packet[16] = ((PKT_LEN-14) >> 8);
    packet[17] = ((PKT_LEN-14)&0Xff);
    packet[38] = ((PKT_LEN-34) >> 8);
    packet[39] = ((PKT_LEN-34)&0Xff);

    struct ibv_device **dev_list;
    struct ibv_device *ib_dev;
    struct ibv_context *context;
    struct ibv_pd *pd;
    int ret;

    char packet_large[PKT_LEN];
    memset(packet_large, 2, sizeof(char)*PKT_LEN);
    memcpy(packet_large, packet, sizeof(packet));

    /*1. Get the list of offload capable devices */
    dev_list = ibv_get_device_list(NULL);
    if (!dev_list) {
        perror("Failed to get devices list");
        exit(1);
    }

    /* In this example, we will use the first adapter (device) we find on the list (dev_list[0]) . 
    You may change the code in case you have a setup with more than one adapter installed. */
    ib_dev = dev_list[DEV_NO];
    if (!ib_dev) {
        fprintf(stderr, "IB device not found\n");
        exit(1);
    }
    const char *dev_name;
    dev_name = ibv_get_device_name(ib_dev);
    printf("dev_name: %s\n",dev_name);

    /* 2. Get the device context */
    /* Get context to device. The context is a descriptor and needed for resource tracking and operations */
    context = ibv_open_device(ib_dev);
    if (!context) {
        fprintf(stderr, "Couldn't get context for %s\n",
        ibv_get_device_name(ib_dev));
        exit(1);
    }

    /* 3. Allocate Protection Domain */
    /* Allocate a protection domain to group memory regions (MR) and rings */
    pd = ibv_alloc_pd(context);
    if (!pd) {
        fprintf(stderr, "Couldn't allocate PD\n");
        exit(1);
    }

    struct ibv_cq *cq;
    cq = ibv_create_cq(context, SQ_NUM_DESC, NULL, NULL, 0);
    if (!cq) {
        fprintf(stderr, "Couldn't create CQ %d\n", errno);
        exit (1);
    }
    /* 5. Initialize QP */
    //struct ibv_qp *qp;
    struct ibv_qp *qp;
    struct ibv_qp_init_attr qp_init_attr = {
        .qp_context = NULL,
        /* report send completion to cq */
        .send_cq = cq,
        .recv_cq = cq,
        .cap = {
            /* number of allowed outstanding sends without waiting for a completion */
            .max_send_wr = SQ_NUM_DESC,
            /* maximum number of pointers in each descriptor */
            .max_send_sge = 1,
            /* if inline maximum of payload data in the descriptors themselves */
            //.max_inline_data = 512,
            .max_recv_wr = 0
        },
        .qp_type = IBV_QPT_RAW_PACKET,
    };

    /* 6. Create Queue Pair (QP) - Send Ring */
    
    qp = ibv_create_qp(pd, &qp_init_attr);
    if (!qp) {
        fprintf(stderr, "Couldn't create RSS QP\n");
        exit(1);
    }

    /* 7. Initialize the QP (receive ring) and assign a port */
    struct ibv_qp_attr qp_attr;
    int qp_flags;
    memset(&qp_attr, 0, sizeof(qp_attr));
    qp_flags = IBV_QP_STATE | IBV_QP_PORT;
    qp_attr.qp_state = IBV_QPS_INIT;
    qp_attr.port_num = 1;
   
    ret = ibv_modify_qp(qp, &qp_attr, qp_flags);
    if (ret < 0) {
        fprintf(stderr, "failed modify qp to init\n");
        exit(1);
    }
    
    memset(&qp_attr, 0, sizeof(qp_attr));

    /* 8. Move the ring to ready to send in two steps (a,b) */
    /* a. Move ring state to ready to receive, this is needed to be able to 
    move ring to ready to send even if receive queue is not enabled */
    qp_flags = IBV_QP_STATE;
    qp_attr.qp_state = IBV_QPS_RTR;
    ret = ibv_modify_qp(qp, &qp_attr, qp_flags);
    if (ret < 0) {
        fprintf(stderr, "failed modify qp to receive\n");
        exit(1);
    }

    /* b. Move the ring to ready to send */
    qp_flags = IBV_QP_STATE;
    qp_attr.qp_state = IBV_QPS_RTS;
    ret = ibv_modify_qp(qp, &qp_attr, qp_flags);
    if (ret < 0) {
        fprintf(stderr, "failed modify qp to receive\n");
        exit(1);
    }

    /* 9. Allocate Memory */
    int buf_size = PKT_LEN*SQ_NUM_DESC; /* maximum size of data to be access directly by hw */
    void *buf;
    buf = malloc(buf_size);
    if (!buf) {
        fprintf(stderr, "Coudln't allocate memory\n");
        exit(1);
    }
    
    /* 10. Register the user memory so it can be accessed by the HW directly */
    
    struct ibv_mr *mr;
    mr = ibv_reg_mr(pd, buf, buf_size, IBV_ACCESS_LOCAL_WRITE);
    if (!mr) {
        fprintf(stderr, "Couldn't register mr\n");
        exit(1);
    }


    int n;
    struct ibv_sge sg_entry[SQ_NUM_DESC];
    struct ibv_send_wr wr[SQ_NUM_DESC], *bad_wr;
    int msgs_completed;
    struct ibv_wc wc;

    /* scatter/gather entry describes location and size of data to send*/
    

    for(int i=0;i<SQ_NUM_DESC;i++)
        memset(&wr[i], 0, sizeof(wr[i]));

    /*
    * descriptor for send transaction - details:
    * - how many pointer to data to use
    * - if this is a single descriptor or a list (next == NULL single)
    * - if we want inline and/or completion
    */
    struct packet *pkt = (struct packet*)packet_large;
    printf("dst_mac: %02x, %02x, %02x, %02x, %02x, %02x\n", pkt->dst_mac[0],pkt->dst_mac[1],pkt->dst_mac[2],pkt->dst_mac[3],pkt->dst_mac[4],pkt->dst_mac[5]);
    printf("src_mac: %02x, %02x, %02x, %02x, %02x, %02x\n", pkt->src_mac[0],pkt->src_mac[1],pkt->src_mac[2],pkt->src_mac[3],pkt->src_mac[4],pkt->src_mac[5]);
    printf("dst_ip: %d.%d.%d.%d\n", pkt->dst_ip[0], pkt->dst_ip[1], pkt->dst_ip[2], pkt->dst_ip[3]);
    printf("src_ip: %d.%d.%d.%d\n", pkt->src_ip[0], pkt->src_ip[1], pkt->src_ip[2], pkt->src_ip[3]);
    
    for(int i=0;i<SQ_NUM_DESC;i++)
    {
        wr[i].num_sge = 1;
        *((uint64_t *)((struct packet*)packet_large)->mcnt) = i;
        memcpy((uint8_t*)buf + i * PKT_LEN, packet_large, sizeof(packet_large));
        sg_entry[i].length = sizeof(packet_large);
        sg_entry[i].lkey = mr->lkey;
        sg_entry[i].addr = (uint64_t)buf + i * PKT_LEN;
        wr[i].sg_list = &sg_entry[i];
        wr[i].next = NULL;
        wr[i].opcode = IBV_WR_SEND;
    }
    /* 
    for(int i=0; i<SQ_NUM_DESC; i++)
        printf("mcnt %02d: %ld\n",i, *(uint64_t*)((struct packet*)(wr[i].sg_list->addr))->mcnt);
    */
    /* 10. Send Operation */
    while(1) {
        for(int i=0;i<SQ_NUM_DESC;i++)
        {
            wr[i].wr_id = i;
            wr[i].send_flags |= IBV_SEND_SIGNALED;
            ret = ibv_post_send(qp, &wr[i], &bad_wr);
            if (ret < 0) {
                fprintf(stderr, "failed in post send\n");
                exit(1);
            } 
            msgs_completed = ibv_poll_cq(cq, 1, &wc);
			//while(msgs_completed == 0)
			//	msgs_completed = ibv_poll_cq(cq, 1, &wc);
			/*
            if (msgs_completed > 0) {
                printf("completed message %ld\n", wc.wr_id);
            } else if (msgs_completed < 0) {
                printf("Polling error\n");
                exit(1);
            }
            */
           if (msgs_completed < 0) {
                printf("Polling error\n");
                exit(1);
            }
		   for(int i = 0; i<100; i++); //300Gbps
		   //for(int i = 0; i<200; i++); //250Gbps
           //for(int i = 0; i<500; i++); //200Gbps
		   //for(int i = 0; i<1000; i++); //30Gbps
        }
    }
    printf("We are done\n");
    return 0;
}
