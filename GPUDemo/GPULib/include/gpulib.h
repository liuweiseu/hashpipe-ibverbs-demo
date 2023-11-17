#ifndef _GPULIB_H_
#define _GPULIB_H_

#define STREAM_NUM 2

void GPU_Init();
void GPU_GetDevInfo();
int GPU_SetDevice(int gpu_dev);
void GPU_MallocBuffer(void **buf, uint64_t size);
void Host_MallocBuffer(void **buf, int size);
float GPU_MoveDataFromHost(void *src, void *dst, int size);
int GPU_CreateStream();
float GPU_MoveDataFromHostAsync(void *src, void *dst, int size, int i);
int GPU_StreamSync(int i);
int GPU_DestroyStream();
void GPU_MoveDataToHost(void *src, void *dst, int size);
void GPU_FreeBuffer(void *buf);
void Host_FreeBuffer(void *buf);
int Host_PinMem(void *buf, int size);
void Host_UnpinMem(void *buf);

#endif
