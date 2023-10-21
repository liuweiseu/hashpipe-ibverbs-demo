#include <stdio.h>
#include <stdlib.h>

#include "cuda.h"

extern "C" {
#include "gpulib.h"

void GPU_GetDevInfo()
{
    int nDevices;
  	cudaGetDeviceCount(&nDevices);
	printf("\nGPUs on this system:\n");
  	for (int i = 0; i < nDevices; i++) {
    	cudaDeviceProp prop;
    	cudaGetDeviceProperties(&prop, i);
    	printf("Device Number: %d\n", i);
    	printf("  Device name: %s\n", prop.name);
    	printf("  Memory Clock Rate (KHz): %d\n", prop.memoryClockRate);
    	printf("  Memory Bus Width (bits): %d\n", prop.memoryBusWidth);
    	printf("  Peak Memory Bandwidth (GB/s): %f\n\n", 2.0*prop.memoryClockRate*(prop.memoryBusWidth/8)/1.0e6);
  	}
}

int GPU_SetDevice(int gpu_dev)
{
    int rv = cudaSetDevice(gpu_dev);
    cudaDeviceProp prop;
    int deviceID;
    cudaGetDevice(&deviceID);
    cudaGetDeviceProperties(&prop, deviceID);
    printf("The selected GPU Device Info:\r\n");
    printf("%-25s: %d\r\n", "MaxThreadsPerBlock", prop.maxThreadsPerBlock);
    printf("%-25s: %d %d %d\r\n","maxThreadsDim", prop.maxThreadsDim[0], \
                                                  prop.maxThreadsDim[1], \
                                                  prop.maxThreadsDim[2]);
    printf("%-25s: %d %d %d\r\n","maxGridSize",   prop.maxGridSize[0], \
                                                  prop.maxGridSize[1], \
                                                  prop.maxGridSize[2]);
    if(!prop.deviceOverlap)
        return 1;
    else
        return 0;
}

void GPU_MallocBuffer(void *buf, int size)
{
    cudaMalloc((void**)&buf, size);
}

void GPU_MoveDataFromHost(void *src, void *dst, int size)
{
    cudaMemcpy(dst, src, size, cudaMemcpyHostToDevice);
}

void GPU_MoveDataToHost(void *src, void *dst, int size)
{
    cudaMemcpy(dst, src, size, cudaMemcpyDeviceToHost);
}

void GPU_FreeBuffer(void *buf)
{
     cudaFree(buf);
}
}