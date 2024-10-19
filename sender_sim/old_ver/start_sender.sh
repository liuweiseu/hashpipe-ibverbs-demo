#!/bin/bash

numactl --physcpubind=19 --membind 0 ./sender -l 400