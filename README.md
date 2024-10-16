# HASHPIPE-Ibverbs-Demo
This is the demo code for [hashpipe](https://github.com/david-macmahon/hashpipe) embedded ibverbs.  
The code was referred from [here](https://github.com/MydonSolutions/hpguppi_daq/blob/seti-ata-8bit/src/hpguppi_ibverbs_pkt_thread.c).
# Platform info
You need to change a bit of code for your own platform:
* scripts/start_hp_ibv_demo.sh:   
  `IBVIFACE=enp132s0f0np0`, which defines the port for receing packets.
* src/ibverbs_pkt_recv_thread.c:  
  line 421 to line 427 defines the udp info.
    ```
    uint8_t src_uint8_t src_mac[6] = {0x0c, 0x42, 0xa1, 0xbe, 0x34, 0xf8};
    uint32_t  src_ip = 0xc0a80202;
    uint32_t  dst_ip = 0xc0a80228;
    uint16_t  src_port = 49152;
    uint16_t  dst_port = 49152;
    ```
# HASHPIPE requirement
Please use the following branch:
```
$ git clone -b seti https://github.com/MydonSolutions/hashpipe
$ cd hashpipe
$ git checkout 81a79e626d4fe78f3f7cc6209be45b8569fae42d
$ autoreconf -is && ./configure && make
```
# How to use
1. compile the code
    ```
    make
    ```
2. install the lib and scripts
    ```
    sudo make install
    ```
3. start the hashpipe instance
    ```
    sudo start_hp_ibv_demo.sh
    ```
