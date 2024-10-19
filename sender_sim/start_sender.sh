#!/bin/bash

#numactl --physcpubind=19 --membind 0 ./sender -l 400
numactl --physcpubind=19 --membind 0 SenderSim -d 0 --smac a0:88:c2:0d:5e:28 --dmac 94:6d:ae:ac:f8:38 --sip 192.168.3.2 --dip 192.168.3.12 --sport 49152 --dport 49152 --inf 2>> ibv_info.log