/*************************************************************************
	> File Name: demo.c
	> Author: Wei Liu
	> Mail: liuwei_berkeley@berkeley.edu
	> Created Time: Sat 21 Oct 2023 03:04:18 AM UTC
 ************************************************************************/

#include<stdio.h>
#include "gpulib.h"


void main()
{
	int status = 0;
    
    // Check gpu status
    GPU_GetDevInfo();
    status = GPU_SetDevice(0);
    if(status != 0)
        printf("No device will handle overlaps.\r\n");
    else   
        printf("overlaps are supported on the device.\r\n");

}

