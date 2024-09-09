#include <stdio.h>
#include <cuda.h>

int main()
{
	void *host_buf, *gpu_buf;
	int size = 8192 * 16384;
	cudaEvent_t startEvent, stopEvent;
	float transfer_time, gbps;

	cudaEventCreate(&startEvent);
	cudaEventCreate(&stopEvent);

	cudaMallocHost((void**)&host_buf, size);
	cudaMalloc((void **)&gpu_buf, size);

	cudaEventRecord(startEvent, 0);
	cudaMemcpy(gpu_buf, host_buf, size, cudaMemcpyHostToDevice);
	cudaEventRecord(stopEvent, 0);
	cudaEventSynchronize(stopEvent);
	cudaEventElapsedTime(&transfer_time, startEvent, stopEvent);
	gbps = size * 1e-6  / transfer_time;
	printf("transfer_time: %f\n", transfer_time);
	printf("Gbps: %.2f Gbps\n", gbps);

	cudaFree(gpu_buf);
	cudaFreeHost(host_buf);
}
