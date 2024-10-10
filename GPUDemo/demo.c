/************************************************************************
	> File Name: demo.c
	> Author: Wei Liu
	> Mail: liuwei_berkeley@berkeley.edu
	> Created Time: Sat 21 Oct 2023 03:04:18 AM UTC
 ************************************************************************/

#include<stdio.h>
#include <stdint.h>
#include <stdlib.h>
//#include <cuda.h> 

#include "gpulib.h"

int main()
{
	int status = 0;
    
    // Check gpu status
    GPU_Init();
	GPU_GetDevInfo();
    status = GPU_SetDevice(0);
    if(status != 0)
        printf("No device will handle overlaps.\r\n");
    else   
        printf("overlaps are supported on the device.\r\n");
	uint8_t *host_buf, *gpu_buf;
	int size = 8192*16384;
	float gbps = 0;
	Host_MallocBuffer((void**)&host_buf, size);

	if(host_buf==NULL) printf("Allocate mem failed.\n");
	for(int i=0; i<size; i++)
		host_buf[i] = i&0xff;
	GPU_MallocBuffer((void**)&gpu_buf, size);
	gbps = GPU_MoveDataFromHost(host_buf, gpu_buf, size);
	printf("Bandwidth: %.2f Gbps\n", gbps);
	Host_FreeBuffer(host_buf);
	GPU_FreeBuffer(gpu_buf);
	
	return 0;
}

