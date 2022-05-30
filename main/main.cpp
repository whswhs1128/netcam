
#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* End of #ifdef __cplusplus */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>

#include "sample_comm.h"

/******************************************************************************
* function    : main()
* Description : video venc sample
******************************************************************************/

extern "C" int rtsp_start();

int main(int argc, char *argv[])
{
    HI_S32 s32Ret = HI_SUCCESS;
    HI_U32 u32Index;

    if (argc < 2)
    {
        //SAMPLE_VENC_Usage(argv[0]);
        return HI_FAILURE;
    }
    u32Index = atoi(argv[1]);

    //signal(SIGINT, SAMPLE_VENC_HandleSig);
    //signal(SIGTERM, SAMPLE_VENC_HandleSig);

    s32Ret = rtsp_start();

    if (HI_SUCCESS == s32Ret)
    { printf("program exit normally!\n"); }
    else
    { printf("program exit abnormally!\n"); }

    exit(s32Ret);
}

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* End of #ifdef __cplusplus */
