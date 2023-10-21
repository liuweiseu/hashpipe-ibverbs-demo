#ifndef _GPULIB_H_
#define _GPULIB_H_

void GPU_GetDevInfo();
int GPU_SetDevice(int gpu_dev);
void GPU_MallocBuffer(void *buf, int size);
void GPU_MoveDataFromHost(void *src, void *dst, int size);
void GPU_MoveDataToHost(void *src, void *dst, int size);
void GPU_FreeBuffer(void *buf);

#endif