#! /bin/bash

numactl --physcpubind=16-18 hashpipe -p hp_ibv_demo.so -I 0 -o IBVIFACE=ens4np0 -o MAXFLOWS=16 -o IBVSNIFF=-1 -c 18 ibverbs_pkt_recv_thread -c 17 gpu_thread -c 16 output_thread
