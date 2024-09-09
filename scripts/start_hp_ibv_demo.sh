#! /bin/bash

hashpipe -p hp_ibv_demo.so -I 0 -o IBVIFACE=ens4np0 -o MAXFLOWS=16 -o IBVSNIFF=-1 ibverbs_pkt_recv_thread gpu_thread output_thread
