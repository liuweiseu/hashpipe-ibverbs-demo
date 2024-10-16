#! /bin/bash

numactl --physcpubind=7-9 --membind 0 hashpipe -p hp_ibv_demo.so -I 0 -o IBVIFACE=ens4np0 -o MAXFLOWS=16 -o IBVSNIFF=49152 -c 8 ibverbs_pkt_recv_thread -c 7 gpu_thread -c 9 output_thread
