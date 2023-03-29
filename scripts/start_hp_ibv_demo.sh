#! /bin/bash

hashpipe -p hp_ibv_demo.so -I 0 -o IBVIFACE=enp132s0f0np0 -o MAXFLOWS=1 -o IBVSNIFF=-1 ibverbs_pkt_recv_thread gpu_thread output_thread