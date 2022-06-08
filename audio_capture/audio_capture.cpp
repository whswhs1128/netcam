#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>

#include<sys/ipc.h>
#include<sys/shm.h>

#include "sample_comm.h"

#include "acodec.h"
#include "audio_aac_adp.h"
#include "audio_dl_adp.h"

#include "hi_resampler_api.h"
#include "hi_vqe_register_api.h"
#include "rtsp_demo.h"
#include "comm.h"

//#include "liveMedia.hh"
#include "audio_capture.h"
////int g_aid_shm = -1;
//struct audio_shm_sync_st *g_p_a_shm = (audio_shm_sync_st*)-1;//指向shm

typedef struct {
  rtsp_demo_handle g_rtsplive = NULL;
  rtsp_session_handle session= NULL;
}rtsp_handle_struct;

rtsp_demo_handle q_g_rtsplive = NULL;
rtsp_session_handle q_session= NULL;


typedef struct tagSAMPLE_AENC_S
{
    HI_BOOL bStart;
    pthread_t stAencPid;
    HI_S32  AeChn;
    HI_S32  AdChn;
    FILE*    pfd;
    HI_BOOL bSendAdChn;
} SAMPLE_AENC_S;

extern int g_s32Quit;

static PAYLOAD_TYPE_E gs_enPayloadType = PT_AAC;

static HI_BOOL gs_bAioReSample  = HI_FALSE;
static HI_BOOL gs_bUserGetMode  = HI_FALSE;
static HI_BOOL gs_bAoVolumeCtrl = HI_FALSE;
static AUDIO_SAMPLE_RATE_E enInSampleRate  = AUDIO_SAMPLE_RATE_BUTT;
static AUDIO_SAMPLE_RATE_E enOutSampleRate = AUDIO_SAMPLE_RATE_BUTT;

static AUDIO_SAMPLE_RATE_E g_enWorkSampleRate = AUDIO_SAMPLE_RATE_48000;
static AUDIO_SOUND_MODE_E g_enSoundmode = AUDIO_SOUND_MODE_STEREO;
static HI_U32 g_u32ChnCnt = 2;

/* 0: close, 1: record, 2: talk*/
static HI_U32 u32AiVqeType = 2;

#define SAMPLE_DBG(s32Ret)\
    do{\
        printf("s32Ret=%#x,fuc:%s,line:%d\n", s32Ret, __FUNCTION__, __LINE__);\
    }while(0)

#define SAMPLE_RES_CHECK_NULL_PTR(ptr)\
    do{\
        if(NULL == (HI_U8*)(ptr) )\
        {\
            printf("ptr is NULL,fuc:%s,line:%d\n", __FUNCTION__, __LINE__);\
            return NULL;\
        }\
    }while(0)

#define RES_LIB_NAME "libhive_RES.so"
#define GK7205V200 0x636a1010
#define CHIP_ID_GK7202V300 0x636a1012

/******************************************************************************
* function : PT Number to String
******************************************************************************/
static char* SAMPLE_AUDIO_Pt2Str(PAYLOAD_TYPE_E enType)
{
    if (PT_G711A == enType)
    {
        return "g711a";
    }
    else if (PT_G711U == enType)
    {
        return "g711u";
    }
    else if (PT_ADPCMA == enType)
    {
        return "adpcm";
    }
    else if (PT_G726 == enType)
    {
        return "g726";
    }
    else if (PT_LPCM == enType)
    {
        return "pcm";
    }
    else if (PT_AAC == enType)
    {
        return "aac";
    }
    else
    {
        return "data";
    }
}


typedef struct tagSAMPLE_AI2EXTRES_S
{
    HI_BOOL bStart;
    HI_S32  AiDev;
    HI_S32  AiChn;
    AUDIO_SAMPLE_RATE_E enInSample;
    AUDIO_SAMPLE_RATE_E enOutSample;
    HI_U32 u32PerFrame;
    FILE* pfd;
    pthread_t stAiPid;
} SAMPLE_AI2EXTRES_S;

typedef HI_VOID* (*pHI_Resampler_Create_Callback)(HI_S32 s32Inrate, HI_S32 s32Outrate, HI_S32 s32Chans);
typedef HI_S32 (*pHI_Resampler_Process_Callback)(HI_VOID* inst, HI_S16* s16Inbuf, HI_S32 s32Insamps, HI_S16* s16Outbuf);
typedef HI_VOID (*pHI_Resampler_Destroy_Callback)(HI_VOID* inst);
typedef HI_S32 (*pHI_Resampler_GetMaxOutputNum_Callback)(HI_VOID* inst, HI_S32 s32Insamps);

typedef struct
{
    HI_VOID *pLibHandle;

    pHI_Resampler_Create_Callback pHI_Resampler_Create;
    pHI_Resampler_Process_Callback pHI_Resampler_Process;
    pHI_Resampler_Destroy_Callback pHI_Resampler_Destroy;
    pHI_Resampler_GetMaxOutputNum_Callback pHI_Resampler_GetMaxOutputNum;
} SAMPLE_RES_FUN_S;

static SAMPLE_AI2EXTRES_S   gs_stSampleAiExtRes[AI_DEV_MAX_NUM * AI_MAX_CHN_NUM];
static SAMPLE_RES_FUN_S     gs_stSampleResFun = {0};

/******************************************************************************
* function : get frame from Ai, send it  to Resampler
******************************************************************************/
void* SAMPLE_COMM_AUDIO_AiExtResProc(void* parg)
{
    HI_S32 s32Ret;
    HI_S32 AiFd;
    SAMPLE_AI2EXTRES_S* pstAiCtl = (SAMPLE_AI2EXTRES_S*)parg;
    AUDIO_FRAME_S stFrame;
    AEC_FRAME_S   stAecFrm;
    fd_set read_fds;
    struct timeval TimeoutVal;
    AI_CHN_PARAM_S stAiChnPara;
    HI_U32 u32CacheCount = 0;
    HI_S16 s16Cache[16]; /*Max 64/8 * 2.*/
    HI_VOID* hRes;
    HI_S16* ps16OutBuf = HI_NULL;
    HI_U32 u32ProcFrame = 0;
    HI_U32 u32OutSample = 0;

    SAMPLE_RES_CHECK_NULL_PTR(gs_stSampleResFun.pHI_Resampler_Create);
    SAMPLE_RES_CHECK_NULL_PTR(gs_stSampleResFun.pHI_Resampler_Process);
    SAMPLE_RES_CHECK_NULL_PTR(gs_stSampleResFun.pHI_Resampler_Destroy);
    SAMPLE_RES_CHECK_NULL_PTR(gs_stSampleResFun.pHI_Resampler_GetMaxOutputNum);

    s32Ret = HI_MPI_AI_GetChnParam(pstAiCtl->AiDev, pstAiCtl->AiChn, &stAiChnPara);
    if (HI_SUCCESS != s32Ret)
    {
        printf("%s: Get ai chn param failed\n", __FUNCTION__);
        return NULL;
    }

    stAiChnPara.u32UsrFrmDepth = 30;

    s32Ret = HI_MPI_AI_SetChnParam(pstAiCtl->AiDev, pstAiCtl->AiChn, &stAiChnPara);
    if (HI_SUCCESS != s32Ret)
    {
        printf("%s: set ai chn param failed\n", __FUNCTION__);
        return NULL;
    }

    /*Create Resample.*/
    /* only support mono channel. */
    hRes = gs_stSampleResFun.pHI_Resampler_Create(pstAiCtl->enInSample, pstAiCtl->enOutSample, 1);

    ps16OutBuf = (HI_S16*)malloc(gs_stSampleResFun.pHI_Resampler_GetMaxOutputNum(hRes, pstAiCtl->u32PerFrame) * sizeof(HI_S16) + 2);

#if 1
    HI_S32 s32Mulit = 1;
    if (pstAiCtl->enInSample % pstAiCtl->enOutSample == 0)
    {
        s32Mulit = pstAiCtl->enInSample / pstAiCtl->enOutSample;
        if (pstAiCtl->u32PerFrame % s32Mulit == 0)
        {
            s32Mulit = 1;
        }
    }
#endif

    FD_ZERO(&read_fds);
    AiFd = HI_MPI_AI_GetFd(pstAiCtl->AiDev, pstAiCtl->AiChn);
    FD_SET(AiFd, &read_fds);

    while (pstAiCtl->bStart)
    {
        TimeoutVal.tv_sec = 1;
        TimeoutVal.tv_usec = 0;

        FD_ZERO(&read_fds);
        FD_SET(AiFd, &read_fds);

        s32Ret = select(AiFd + 1, &read_fds, NULL, NULL, &TimeoutVal);
        if (s32Ret < 0)
        {
            break;
        }
        else if (0 == s32Ret)
        {
            printf("%s: get ai frame select time out\n", __FUNCTION__);
            break;
        }

        if (FD_ISSET(AiFd, &read_fds))
        {
            /* get frame from ai chn */
            memset(&stAecFrm, 0, sizeof(AEC_FRAME_S));
            s32Ret = HI_MPI_AI_GetFrame(pstAiCtl->AiDev, pstAiCtl->AiChn, &stFrame, &stAecFrm, HI_FALSE);
            if (HI_SUCCESS != s32Ret )
            {
#if 0
                printf("%s: HI_MPI_AI_GetFrame(%d, %d), failed with %#x!\n", \
                       __FUNCTION__, pstAiCtl->AiDev, pstAiCtl->AiChn, s32Ret);
                pstAiCtl->bStart = HI_FALSE;
                return NULL;
#else
                continue;
#endif
            }

            /* send frame to encoder */
            if (1 != s32Mulit)
            {
                if (0 != u32CacheCount)
                {
                    memcpy(s16Cache + u32CacheCount*sizeof(HI_S16), stFrame.u64VirAddr[0], (s32Mulit - u32CacheCount)*sizeof(HI_S16));
                    u32OutSample = gs_stSampleResFun.pHI_Resampler_Process(hRes, s16Cache, s32Mulit, ps16OutBuf);
                    (HI_VOID)fwrite(ps16OutBuf, 2, u32OutSample, pstAiCtl->pfd);
                }
                u32ProcFrame = pstAiCtl->u32PerFrame - (s32Mulit - u32CacheCount);
                u32ProcFrame = u32ProcFrame - u32ProcFrame % s32Mulit;
                u32OutSample = gs_stSampleResFun.pHI_Resampler_Process(hRes, (HI_S16*)(stFrame.u64VirAddr[0] + (s32Mulit - u32CacheCount)*sizeof(HI_S16)), u32ProcFrame, ps16OutBuf);
                (HI_VOID)fwrite(ps16OutBuf, 2, u32OutSample, pstAiCtl->pfd);

                if ((pstAiCtl->u32PerFrame - u32ProcFrame - (s32Mulit - u32CacheCount)) != 0)
                {
                    memcpy(s16Cache, stFrame.u64VirAddr[0] + (s32Mulit - u32CacheCount + u32ProcFrame)*sizeof(HI_S16), \
                        (pstAiCtl->u32PerFrame - u32ProcFrame - (s32Mulit - u32CacheCount))*sizeof(HI_S16));
                }
                u32CacheCount = pstAiCtl->u32PerFrame - u32ProcFrame - (s32Mulit - u32CacheCount);
            }
            else
            {
                u32OutSample = gs_stSampleResFun.pHI_Resampler_Process(hRes, (HI_S16*)stFrame.u64VirAddr[0], pstAiCtl->u32PerFrame, ps16OutBuf);
                (HI_VOID)fwrite(ps16OutBuf, 2, u32OutSample, pstAiCtl->pfd);
            }

            fflush(pstAiCtl->pfd);

            /* finally you must release the stream */
            s32Ret = HI_MPI_AI_ReleaseFrame(pstAiCtl->AiDev, pstAiCtl->AiChn, &stFrame, &stAecFrm);
            if (HI_SUCCESS != s32Ret )
            {
                printf("%s: HI_MPI_AI_ReleaseFrame(%d, %d), failed with %#x!\n", \
                       __FUNCTION__, pstAiCtl->AiDev, pstAiCtl->AiChn, s32Ret);
                pstAiCtl->bStart = HI_FALSE;
                free(ps16OutBuf);
                gs_stSampleResFun.pHI_Resampler_Destroy(hRes);
                return NULL;
            }
        }
    }

    pstAiCtl->bStart = HI_FALSE;
    free(ps16OutBuf);
    gs_stSampleResFun.pHI_Resampler_Destroy(hRes);

    return NULL;
}

/******************************************************************************
* function : Create the thread to get frame from ai and send to aenc
******************************************************************************/
HI_S32 SAMPLE_COMM_AUDIO_CreatTrdAiExtRes(AIO_ATTR_S* pstAioAttr, AUDIO_DEV AiDev, AI_CHN AiChn,  AUDIO_SAMPLE_RATE_E enOutSampleRate, FILE* pResFd)
{
    SAMPLE_AI2EXTRES_S* pstAi2ExtRes = NULL;

    pstAi2ExtRes = &gs_stSampleAiExtRes[AiDev * AI_MAX_CHN_NUM + AiChn];
    pstAi2ExtRes->AiDev = AiDev;
    pstAi2ExtRes->AiChn = AiChn;
    pstAi2ExtRes->enInSample = pstAioAttr->enSamplerate;
    pstAi2ExtRes->enOutSample = enOutSampleRate;
    pstAi2ExtRes->u32PerFrame = pstAioAttr->u32PtNumPerFrm;
    pstAi2ExtRes->pfd = pResFd;
    pstAi2ExtRes->bStart = HI_TRUE;
    pthread_create(&pstAi2ExtRes->stAiPid, 0, SAMPLE_COMM_AUDIO_AiExtResProc, pstAi2ExtRes);

    return HI_SUCCESS;
}

/******************************************************************************
* function : Destory the thread to get frame from ai and send to extern resamler
******************************************************************************/
HI_S32 SAMPLE_COMM_AUDIO_DestoryTrdAiExtRes(AUDIO_DEV AiDev, AI_CHN AiChn)
{
    SAMPLE_AI2EXTRES_S* pstAi = NULL;

    pstAi = &gs_stSampleAiExtRes[AiDev * AI_MAX_CHN_NUM + AiChn];
    if (pstAi->bStart)
    {
        pstAi->bStart = HI_FALSE;
        //pthread_cancel(pstAi->stAiPid);
        pthread_join(pstAi->stAiPid, 0);
    }
    fclose(pstAi->pfd);

    return HI_SUCCESS;
}

#if !defined(HI_VQE_USE_STATIC_MODULE_REGISTER) || !defined(HI_AAC_USE_STATIC_MODULE_REGISTER)
/******************************************************************************
* function : Add dynamic load path
******************************************************************************/
static HI_VOID SAMPLE_AUDIO_AddLibPath(HI_VOID)
{
    HI_S32 s32Ret;
    HI_CHAR aszLibPath[FILE_NAME_LEN] = {0};
#ifdef __HuaweiLite__
    snprintf(aszLibPath, FILE_NAME_LEN, "/sharefs/");
#else
#endif
    s32Ret = Audio_Dlpath(aszLibPath);
    if(HI_SUCCESS != s32Ret)
    {
       printf("%s: add lib path %s failed\n", __FUNCTION__, aszLibPath);
    }
    return;
}
#endif

/******************************************************************************
* function : DeInit resamle functions
******************************************************************************/
static HI_S32 SAMPLE_AUDIO_DeInitExtResFun(HI_VOID)
{
#if defined(HI_VQE_USE_STATIC_MODULE_REGISTER)
    memset(&gs_stSampleResFun, 0, sizeof(SAMPLE_RES_FUN_S));
#else
    if(HI_NULL != gs_stSampleResFun.pLibHandle)
    {
        Audio_Dlclose(gs_stSampleResFun.pLibHandle);
        memset(&gs_stSampleResFun, 0, sizeof(SAMPLE_RES_FUN_S));
    }
#endif

    return HI_SUCCESS;
}

/******************************************************************************
* function : Init resamle functions
******************************************************************************/
static HI_S32 SAMPLE_AUDIO_InitExtResFun(HI_VOID)
{
    HI_S32 s32Ret;
    SAMPLE_RES_FUN_S stSampleResFun;

    s32Ret = SAMPLE_AUDIO_DeInitExtResFun();
    if(HI_SUCCESS != s32Ret)
    {
        printf("[Func]:%s [Line]:%d [Info]:%s\n",
            __FUNCTION__, __LINE__, "Unload resample lib fail!\n");
        return HI_FAILURE;
    }

    memset(&stSampleResFun, 0, sizeof(SAMPLE_RES_FUN_S));

#if defined(HI_VQE_USE_STATIC_MODULE_REGISTER)
    stSampleResFun.pHI_Resampler_Create = HI_Resampler_Create;
    stSampleResFun.pHI_Resampler_Process = HI_Resampler_Process;
    stSampleResFun.pHI_Resampler_Destroy = HI_Resampler_Destroy;
    stSampleResFun.pHI_Resampler_GetMaxOutputNum = HI_Resampler_GetMaxOutputNum;

#else
    s32Ret = Audio_Dlopen(&(stSampleResFun.pLibHandle), RES_LIB_NAME);
    if(HI_SUCCESS != s32Ret)
    {
        printf("[Func]:%s [Line]:%d [Info]:%s\n",
            __FUNCTION__, __LINE__, "load resample lib fail!\n");
        return HI_FAILURE;
    }

    s32Ret = Audio_Dlsym((HI_VOID** )&(stSampleResFun.pHI_Resampler_Create), stSampleResFun.pLibHandle, "HI_Resampler_Create");
    if(HI_SUCCESS != s32Ret)
    {
        printf("[Func]:%s [Line]:%d [Info]:%s\n",
            __FUNCTION__, __LINE__, "find symbol error!\n");
        return HI_FAILURE;
    }

    s32Ret = Audio_Dlsym((HI_VOID** )&(stSampleResFun.pHI_Resampler_Process), stSampleResFun.pLibHandle, "HI_Resampler_Process");
    if(HI_SUCCESS != s32Ret)
    {
        printf("[Func]:%s [Line]:%d [Info]:%s\n",
            __FUNCTION__, __LINE__, "find symbol error!\n");
        return HI_FAILURE;
    }

    s32Ret = Audio_Dlsym((HI_VOID** )&(stSampleResFun.pHI_Resampler_Destroy), stSampleResFun.pLibHandle, "HI_Resampler_Destroy");
    if(HI_SUCCESS != s32Ret)
    {
        printf("[Func]:%s [Line]:%d [Info]:%s\n",
            __FUNCTION__, __LINE__, "find symbol error!\n");
        return HI_FAILURE;
    }

    s32Ret = Audio_Dlsym((HI_VOID** )&(stSampleResFun.pHI_Resampler_GetMaxOutputNum), stSampleResFun.pLibHandle, "HI_Resampler_GetMaxOutputNum");
    if(HI_SUCCESS != s32Ret)
    {
        printf("[Func]:%s [Line]:%d [Info]:%s\n",
            __FUNCTION__, __LINE__, "find symbol error!\n");
        return HI_FAILURE;
    }
#endif

    memcpy(&gs_stSampleResFun, &stSampleResFun, sizeof(SAMPLE_RES_FUN_S));

    return HI_SUCCESS;
}

#if defined(HI_VQE_USE_STATIC_MODULE_REGISTER)
/******************************************************************************
* function : to register vqe module
******************************************************************************/
HI_S32 SAMPLE_AUDIO_RegisterVQEModule(HI_VOID)
{
    HI_S32 s32Ret = HI_SUCCESS;
    AUDIO_VQE_REGISTER_S stVqeRegCfg = {0};

    //Resample
    stVqeRegCfg.stResModCfg.pHandle = HI_VQE_RESAMPLE_GetHandle();

    //RecordVQE
    stVqeRegCfg.stRecordModCfg.pHandle = HI_VQE_RECORD_GetHandle();

    //TalkVQE
    stVqeRegCfg.stHpfModCfg.pHandle = HI_VQE_HPF_GetHandle();
    stVqeRegCfg.stAecModCfg.pHandle = HI_VQE_AEC_GetHandle();
    stVqeRegCfg.stAgcModCfg.pHandle = HI_VQE_AGC_GetHandle();
    stVqeRegCfg.stAnrModCfg.pHandle = HI_VQE_ANR_GetHandle();
    stVqeRegCfg.stEqModCfg.pHandle = HI_VQE_EQ_GetHandle();

    s32Ret = HI_MPI_AUDIO_RegisterVQEModule(&stVqeRegCfg);
    if (s32Ret != HI_SUCCESS)
    {
        printf("%s: register vqe module fail with s32Ret = %d!\n", __FUNCTION__, s32Ret);
        return HI_FAILURE;
    }

    return HI_SUCCESS;
}
#endif

/******************************************************************************
* function : Open Aenc File
******************************************************************************/
static FILE* SAMPLE_AUDIO_OpenAencFile(AENC_CHN AeChn, PAYLOAD_TYPE_E enType)
{
    FILE* pfd;
    HI_CHAR aszFileName[FILE_NAME_LEN] = {0};

    /* create file for save stream*/
#ifdef __HuaweiLite__
    snprintf(aszFileName, FILE_NAME_LEN, "/sharefs/audio_chn%d.%s", AeChn, SAMPLE_AUDIO_Pt2Str(enType));
#else
    snprintf(aszFileName, FILE_NAME_LEN, "audio_chn%d.%s", AeChn, SAMPLE_AUDIO_Pt2Str(enType));
#endif
    pfd = fopen(aszFileName, "w+");
    if (NULL == pfd)
    {
        printf("%s: open file %s failed\n", __FUNCTION__, aszFileName);
        return NULL;
    }
    printf("open stream file:\"%s\" for aenc ok\n", aszFileName);
    return pfd;
}

HI_VOID SAMPLE_AUDIO_Usage(HI_VOID)
{
    printf("\n\n/Usage:./sample_audio <index> <sampleRate> [filePath]/\n");
    printf("\tindex and its function list below\n");
    printf("\t0:  start AI to AO loop\n");
    printf("\t1:  send audio frame to AENC channel from AI, save them\n");
    printf("\t2:  read audio stream from file, decode and send AO\n");
    printf("\t3:  start AI(VQE process), then send to AO\n");
    printf("\t4:  start AI to Extern Resampler\n");
    printf("\n");
    printf("\tsampleRate list:\n");
    printf("\t8000 11025 12000 16000 22050 24000 32000 44100 48000\n");
    printf("\n");
    printf("\tfilePath represents the path of audio file to be decoded, only for sample 2.\n");
    printf("\tdefault filePath: ./audio_chn0.aac\n");
    printf("\n");
    printf("\texample: ./sample_audio 0 48000\n");
}

/******************************************************************************
* function : to process abnormal case
******************************************************************************/
void SAMPLE_AUDIO_HandleSig(HI_S32 signo)
{
    signal(SIGINT, SIG_IGN);
    signal(SIGTERM, SIG_IGN);

    if (SIGINT == signo || SIGTERM == signo)
    {
        SAMPLE_COMM_AUDIO_DestoryAllTrd();
        SAMPLE_COMM_SYS_Exit();
        printf("\033[0;31mprogram exit abnormally!\033[0;39m\n");
    }

    exit(0);
}
#if 0
HI_S32 PutAudioStreamToRingBuffer(AUDIO_STREAM_S *pstStream)
{
  HI_S32 i;
  HI_S32 len = 0;
#if 0
  if(-1 == g_aid_shm)
  {
    key_t key = ftok("shm_rtsp_a",'a');//键一定要唯一
    g_aid_shm = shmget(key, AUDIO_BUF_NUM * sizeof(struct audio_shm_sync_st), IPC_CREAT | 0666);//返回值为id号

    if(g_aid_shm < 0)
    {
      perror("audio:shmget");
      return HI_FAILURE;
    }
  }
  if(g_p_a_shm == (void *)-1)
  {
    g_p_a_shm = (struct audio_shm_sync_st *)shmat(g_aid_shm, NULL, 0);//0表示共享内存可读可写
    if(g_p_a_shm == (void *)-1)
    {
      perror("audio:shmat");
      
      printf("++PutAudioStreamToRingBuffer, mmap shm error!\n");
      return HI_FAILURE;
    }

    struct audio_shm_sync_st *g_pshm_tmp = g_p_a_shm;
    for(i = 0; i < AUDIO_BUF_NUM; i++)
    {
      g_pshm_tmp->iFlag = 0xA5A5A5A5;
      g_pshm_tmp++;
    }
  }
#endif

  if (g_p_a_shm == (void *)-1)
  {
    return HI_FAILURE;
  }

  //先定位缓冲区
  struct audio_shm_sync_st *g_pshm_tmp = g_p_a_shm;
  for(i = 0; i < AUDIO_BUF_NUM; i++)
  {
    if(g_pshm_tmp->iFlag == 0xA5A5A5A5)
    {
      break;
    }
    else
    {
      g_pshm_tmp++;
    }
  }

  if(AUDIO_BUF_NUM == i)
  {
    //无可写缓冲，覆盖，前帧数据会丢失
    //printf("++ no empty audio buffer!\n");
    return HI_FAILURE;
    g_pshm_tmp = g_p_a_shm;
  }

  memcpy(g_pshm_tmp->pu8Addr, pstStream->pStream, pstStream->u32Len);
#if 0
  printf("audio buf(%X): %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X !\n", 
                    g_pshm_tmp->iLen,
                    g_pshm_tmp->pu8Addr[0], g_pshm_tmp->pu8Addr[1], 
                    g_pshm_tmp->pu8Addr[2], g_pshm_tmp->pu8Addr[3], 
                    g_pshm_tmp->pu8Addr[4], g_pshm_tmp->pu8Addr[5], 
                    g_pshm_tmp->pu8Addr[6], g_pshm_tmp->pu8Addr[7], 
                    g_pshm_tmp->pu8Addr[8], g_pshm_tmp->pu8Addr[9], 
                    g_pshm_tmp->pu8Addr[10], g_pshm_tmp->pu8Addr[11], 
                    g_pshm_tmp->pu8Addr[12], g_pshm_tmp->pu8Addr[13], 
                    g_pshm_tmp->pu8Addr[14], g_pshm_tmp->pu8Addr[15]);
#endif
  g_pshm_tmp->iLen = pstStream->u32Len;
  g_pshm_tmp->iFlag = 0xB4B4B4B4; //等待读取

  //if(shmdt(g_p_a_shm) < 0)
  //{
  //  perror("shmdt");
  //}

  return HI_SUCCESS;
}
#endif
/******************************************************************************
* function : get stream from Aenc, send it  to rtsp
******************************************************************************/
void* AencProc(void* parg)
{
    HI_S32 s32Ret;
    HI_S32 AencFd;
    SAMPLE_AENC_S* pstAencCtl = (SAMPLE_AENC_S*)parg;
    AUDIO_STREAM_S stStream;
    fd_set read_fds;
    struct timeval TimeoutVal;

    FD_ZERO(&read_fds);
    AencFd = HI_MPI_AENC_GetFd(pstAencCtl->AeChn);
    FD_SET(AencFd, &read_fds);

    unsigned char* pStremData = NULL;
    int nSize = 0;
    int i;

    while (pstAencCtl->bStart)
    {
        TimeoutVal.tv_sec = 1;
        TimeoutVal.tv_usec = 0;

        FD_ZERO(&read_fds);
        FD_SET(AencFd, &read_fds);

        s32Ret = select(AencFd + 1, &read_fds, NULL, NULL, &TimeoutVal);
        if (s32Ret < 0)
        {
            break;
        }
        else if (0 == s32Ret)
        {
            printf("%s: get aenc stream select time out\n", __FUNCTION__);
            break;
        }

        if (FD_ISSET(AencFd, &read_fds))
        {
            /* get stream from aenc chn */
            s32Ret = HI_MPI_AENC_GetStream(pstAencCtl->AeChn, &stStream, HI_FALSE);
            if (HI_SUCCESS != s32Ret )
            {
                printf("%s: HI_MPI_AENC_GetStream(%d), failed with %#x!\n", \
                       __FUNCTION__, pstAencCtl->AeChn, s32Ret);
                pstAencCtl->bStart = HI_FALSE;
                return NULL;
            }

            //PutAudioStreamToRingBuffer(&stStream);
	   // for (i = 0; i < stStream.u32PackCount; i++)
	   // {
		pStremData = (unsigned char*)stStream.pStream;
		nSize = stStream.u32Len;
		rtsp_tx_audio(q_session,pStremData,nSize,stStream.u64TimeStamp);
	   // }	
#if 0
            /* send stream to decoder and play for testing */
            if (HI_TRUE == pstAencCtl->bSendAdChn)
            {
                s32Ret = HI_MPI_ADEC_SendStream(pstAencCtl->AdChn, &stStream, HI_TRUE);
                if (HI_SUCCESS != s32Ret )
                {
                    printf("%s: HI_MPI_ADEC_SendStream(%d), failed with %#x!\n", \
                           __FUNCTION__, pstAencCtl->AdChn, s32Ret);
                    pstAencCtl->bStart = HI_FALSE;
                    return NULL;
                }
            }

            /* save audio stream to file */
            (HI_VOID)fwrite(stStream.pStream, 1, stStream.u32Len, pstAencCtl->pfd);

            fflush(pstAencCtl->pfd);
#endif
            /* finally you must release the stream */
            s32Ret = HI_MPI_AENC_ReleaseStream(pstAencCtl->AeChn, &stStream);
            if (HI_SUCCESS != s32Ret )
            {
                printf("%s: HI_MPI_AENC_ReleaseStream(%d), failed with %#x!\n", \
                       __FUNCTION__, pstAencCtl->AeChn, s32Ret);
                pstAencCtl->bStart = HI_FALSE;
                return NULL;
            }
        }
    }

    //fclose(pstAencCtl->pfd);
    pstAencCtl->bStart = HI_FALSE;
    return NULL;
}

static SAMPLE_AENC_S gs_stSampleAenc[AENC_MAX_CHN_NUM];

/******************************************************************************
* function : Create the thread to get stream from aenc and send to adec
******************************************************************************/
HI_S32 CreatTrdAenc(AENC_CHN AeChn, ADEC_CHN AdChn, FILE* pAecFd)
{
  SAMPLE_AENC_S* pstAenc = NULL;
#if 0
  if (NULL == pAecFd)
  {
    return HI_FAILURE;
  }
#endif
  pstAenc = &gs_stSampleAenc[AeChn];
  pstAenc->AeChn = AeChn;
  pstAenc->AdChn = AdChn;
  pstAenc->bSendAdChn = HI_TRUE;
  pstAenc->pfd = pAecFd;
  pstAenc->bStart = HI_TRUE;

  pthread_create(&pstAenc->stAencPid, 0, AencProc, pstAenc);

  return HI_SUCCESS;
}

/******************************************************************************
* function    : main()
* Description : video venc sample
******************************************************************************/
void *thAudioCapture(void* parg)
{
  HI_S32 s32Ret = HI_SUCCESS;
  VB_CONFIG_S stVbConf;
  HI_U32 u32Index = 0;
  HI_CHAR* pFilePath = HI_NULL;
  HI_U32 u32ChipId = 0;

  HI_S32 i, j;
  AI_CHN      AiChn;
  AO_CHN      AoChn = 0;
  ADEC_CHN    AdChn = 0;
  HI_S32      s32AiChnCnt;
  HI_S32      s32AoChnCnt;
  HI_S32      s32AencChnCnt;
  AENC_CHN    AeChn = 0;
  FILE*        pfd = NULL;
  AIO_ATTR_S stAioAttr;

  HI_BOOL     bSendAdec = HI_TRUE;
  //g_p_a_shm = (audio_shm_sync_st*)parg;
  pthread_detach(pthread_self());
	rtsp_handle_struct* rtsp_handle = (rtsp_handle_struct*)parg;
        q_g_rtsplive = rtsp_handle->g_rtsplive;
        q_session= rtsp_handle->session;

  signal(SIGINT, SAMPLE_AUDIO_HandleSig);
  signal(SIGTERM, SAMPLE_AUDIO_HandleSig);

#if defined(HI_VQE_USE_STATIC_MODULE_REGISTER)
  SAMPLE_AUDIO_RegisterVQEModule();
#endif

  memset(&stVbConf, 0, sizeof(VB_CONFIG_S));
  s32Ret = SAMPLE_COMM_SYS_Init(&stVbConf);
  if (HI_SUCCESS != s32Ret)
  {
      printf("%s: system init failed with %d!\n", __FUNCTION__, s32Ret);
      return NULL;
  }

  s32Ret = HI_MPI_SYS_GetChipId(&u32ChipId);
  if (HI_SUCCESS != s32Ret)
  {
      printf("%s: get chip id failed with %d!\n", __FUNCTION__, s32Ret);
      return NULL;
  }

  if(GK7205V200 == u32ChipId || CHIP_ID_GK7202V300 == u32ChipId)
  {
      /* GK7205V200 only support mono input/output, GK7202V300 only support mono output */
      g_enSoundmode = AUDIO_SOUND_MODE_MONO;
      g_u32ChnCnt = 1;
  }

#if !defined(HI_VQE_USE_STATIC_MODULE_REGISTER) || !defined(HI_AAC_USE_STATIC_MODULE_REGISTER)
  SAMPLE_AUDIO_AddLibPath();
#endif

  HI_MPI_AENC_AacInit();
  HI_MPI_ADEC_AacInit();

#ifdef HI_ACODEC_TYPE_TLV320AIC31
  AUDIO_DEV   AiDev = SAMPLE_AUDIO_EXTERN_AI_DEV;
  AUDIO_DEV   AoDev = SAMPLE_AUDIO_EXTERN_AO_DEV;
  stAioAttr.enSamplerate   = g_enWorkSampleRate;
  stAioAttr.enBitwidth     = AUDIO_BIT_WIDTH_16;
  stAioAttr.enWorkmode     = AIO_MODE_I2S_MASTER;
  stAioAttr.enSoundmode    = g_enSoundmode;
  stAioAttr.u32EXFlag      = 0;
  stAioAttr.u32FrmNum      = 30;
  stAioAttr.u32PtNumPerFrm = AACLC_SAMPLES_PER_FRAME;
  stAioAttr.u32ChnCnt      = g_u32ChnCnt;
  stAioAttr.u32ClkSel      = 1;
  stAioAttr.enI2sType      = AIO_I2STYPE_EXTERN;
#else
  AUDIO_DEV   AiDev = SAMPLE_AUDIO_INNER_AI_DEV;
  AUDIO_DEV   AoDev = SAMPLE_AUDIO_INNER_AO_DEV;
  stAioAttr.enSamplerate   = g_enWorkSampleRate;
  stAioAttr.enBitwidth     = AUDIO_BIT_WIDTH_16;
  stAioAttr.enWorkmode     = AIO_MODE_I2S_MASTER;
  stAioAttr.enSoundmode    = g_enSoundmode;
  stAioAttr.u32EXFlag      = 0;
  stAioAttr.u32FrmNum      = 30;
  stAioAttr.u32PtNumPerFrm = AACLC_SAMPLES_PER_FRAME;
  stAioAttr.u32ChnCnt      = g_u32ChnCnt;
  stAioAttr.u32ClkSel      = 0;
  stAioAttr.enI2sType      = AIO_I2STYPE_INNERCODEC;
#endif
  gs_bAioReSample = HI_FALSE;
  enInSampleRate  = AUDIO_SAMPLE_RATE_BUTT;
  enOutSampleRate = AUDIO_SAMPLE_RATE_BUTT;

  /********************************************
    step 1: start Ai
  ********************************************/
  s32AiChnCnt = stAioAttr.u32ChnCnt;
  s32Ret = SAMPLE_COMM_AUDIO_StartAi(AiDev, s32AiChnCnt, &stAioAttr, enOutSampleRate, gs_bAioReSample, NULL, 0, -1);
  if (s32Ret != HI_SUCCESS)
  {
      SAMPLE_DBG(s32Ret);
      goto AIAENC_ERR6;
  }

  /********************************************
    step 2: config audio codec
  ********************************************/
  s32Ret = SAMPLE_COMM_AUDIO_CfgAcodec(&stAioAttr);
  if (s32Ret != HI_SUCCESS)
  {
      SAMPLE_DBG(s32Ret);
      goto AIAENC_ERR5;
  }

  /********************************************
    step 3: start Aenc
  ********************************************/
  s32AencChnCnt = stAioAttr.u32ChnCnt >> stAioAttr.enSoundmode;
  s32Ret = SAMPLE_COMM_AUDIO_StartAenc(s32AencChnCnt, &stAioAttr, gs_enPayloadType);
  if (s32Ret != HI_SUCCESS)
  {
      SAMPLE_DBG(s32Ret);
      goto AIAENC_ERR5;
  }

  /********************************************
    step 4: Aenc bind Ai Chn
  ********************************************/
  for (i = 0; i < s32AencChnCnt; i++)
  {
      AeChn = i;
      AiChn = i;

      if (HI_TRUE == gs_bUserGetMode)
      {
          s32Ret = SAMPLE_COMM_AUDIO_CreatTrdAiAenc(AiDev, AiChn, AeChn);
          if (s32Ret != HI_SUCCESS)
          {
              SAMPLE_DBG(s32Ret);
              for (j=0; j<i; j++)
              {
                  SAMPLE_COMM_AUDIO_DestoryTrdAi(AiDev, j);
              }
              goto AIAENC_ERR4;
          }
      }
      else
      {
          s32Ret = SAMPLE_COMM_AUDIO_AencBindAi(AiDev, AiChn, AeChn);
          if (s32Ret != HI_SUCCESS)
          {
              SAMPLE_DBG(s32Ret);
              for (j=0; j<i; j++)
              {
                  SAMPLE_COMM_AUDIO_AencUnbindAi(AiDev, j, j);
              }
              goto AIAENC_ERR4;
          }
      }
      printf("Ai(%d,%d) bind to AencChn:%d ok!\n", AiDev , AiChn, AeChn);
  }

  /********************************************
    step 5: start Adec & Ao. ( if you want )
  ********************************************/
  if (HI_TRUE == bSendAdec)
  {
  #if 0
        s32Ret = SAMPLE_COMM_AUDIO_StartAdec(AdChn, gs_enPayloadType);
        if (s32Ret != HI_SUCCESS)
        {
            SAMPLE_DBG(s32Ret);
            goto AIAENC_ERR3;
        }

        s32AoChnCnt = stAioAttr.u32ChnCnt;
        s32Ret = SAMPLE_COMM_AUDIO_StartAo(AoDev, s32AoChnCnt, &stAioAttr, enInSampleRate, gs_bAioReSample);
        if (s32Ret != HI_SUCCESS)
        {
            SAMPLE_DBG(s32Ret);
            goto AIAENC_ERR2;
        }

        pfd = SAMPLE_AUDIO_OpenAencFile(AdChn, gs_enPayloadType);
        if (!pfd)
        {
            SAMPLE_DBG(HI_FAILURE);
            goto AIAENC_ERR1;
        }
#endif
        s32Ret = CreatTrdAenc(AeChn, AdChn, pfd); //可能有多个通道需要采集
        if (s32Ret != HI_SUCCESS)
        {
            SAMPLE_DBG(s32Ret);
            goto AIAENC_ERR1;
        }
#if 0
        s32Ret = SAMPLE_COMM_AUDIO_AoBindAdec(AoDev, AoChn, AdChn);
        if (s32Ret != HI_SUCCESS)
        {
            SAMPLE_DBG(s32Ret);
            goto AIAENC_ERR0;
        }

        printf("bind adec:%d to ao(%d,%d) ok \n", AdChn, AoDev, AoChn);
#endif
    }

  return NULL;
  printf("\nplease press twice ENTER to exit this sample\n");
  getchar();
  getchar();

  /********************************************
    step 6: exit the process
  ********************************************/
    if (HI_TRUE == bSendAdec)
    {
        s32Ret = SAMPLE_COMM_AUDIO_AoUnbindAdec(AoDev, AoChn, AdChn);
        if (s32Ret != HI_SUCCESS)
        {
            SAMPLE_DBG(s32Ret);
            return NULL;
        }

AIAENC_ERR0:
        s32Ret = SAMPLE_COMM_AUDIO_DestoryTrdAencAdec(AdChn);
        if (s32Ret != HI_SUCCESS)
        {
            SAMPLE_DBG(s32Ret);
        }

AIAENC_ERR1:
        s32Ret |= SAMPLE_COMM_AUDIO_StopAo(AoDev, s32AoChnCnt, gs_bAioReSample);
        if (s32Ret != HI_SUCCESS)
        {
            SAMPLE_DBG(s32Ret);
        }

AIAENC_ERR2:
        s32Ret |= SAMPLE_COMM_AUDIO_StopAdec(AdChn);
        if (s32Ret != HI_SUCCESS)
        {
            SAMPLE_DBG(s32Ret);
        }

    }

AIAENC_ERR3:
  for (i = 0; i < s32AencChnCnt; i++)
  {
      AeChn = i;
      AiChn = i;

      if (HI_TRUE == gs_bUserGetMode)
      {
          s32Ret |= SAMPLE_COMM_AUDIO_DestoryTrdAi(AiDev, AiChn);
          if (s32Ret != HI_SUCCESS)
          {
              SAMPLE_DBG(s32Ret);
          }
      }
      else
      {
          s32Ret |= SAMPLE_COMM_AUDIO_AencUnbindAi(AiDev, AiChn, AeChn);
          if (s32Ret != HI_SUCCESS)
          {
              SAMPLE_DBG(s32Ret);
          }
      }
  }

AIAENC_ERR4:
  s32Ret |= SAMPLE_COMM_AUDIO_StopAenc(s32AencChnCnt);
  if (s32Ret != HI_SUCCESS)
  {
      SAMPLE_DBG(s32Ret);
  }

AIAENC_ERR5:
  s32Ret |= SAMPLE_COMM_AUDIO_StopAi(AiDev, s32AiChnCnt, gs_bAioReSample, HI_FALSE);
  if (s32Ret != HI_SUCCESS)
  {
      SAMPLE_DBG(s32Ret);
  }

AIAENC_ERR6:

  HI_MPI_AENC_AacDeInit();
  HI_MPI_ADEC_AacDeInit();

  SAMPLE_COMM_SYS_Exit();
}

