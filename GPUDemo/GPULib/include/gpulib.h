#ifndef _GPULIB_H_
#define _GPULIB_H_

void GPU_GetDevInfo();
int GPU_SetDevice(int gpu_dev);
void GPU_MallocBuffer(void **buf, int size);
void Host_MallocBuffer(void **buf, int size);
float GPU_MoveDataFromHost(void *src, void *dst, int size);
void GPU_MoveDataToHost(void *src, void *dst, int size);
void GPU_FreeBuffer(void *buf);
void Host_FreeBuffer(void *buf);

#endif
