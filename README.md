# HASHPIPE-Ibverbs-Demo
This is the demo code for [hashpipe](https://github.com/david-macmahon/hashpipe) embedded ibverbs.
# Platform info
You need to change a bit of code for your own platform:
* scripts/start_hp_ibv_demo.sh: `IBVIFACE=enp132s0f0np0`, which defines the port for receing packets.
* src/ibverbs_pkt_recv_thread.c: line 421 to line 427 defines the udp info.
    ```
    uint8_t src_uint8_t src_mac[6] = {0x0c, 0x42, 0xa1, 0xbe, 0x34, 0xf8};
    uint32_t  src_ip = 0xc0a80202;
    uint32_t  dst_ip = 0xc0a80228;
    uint16_t  src_port = 49152;
    uint16_t  dst_port = 49152;
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
