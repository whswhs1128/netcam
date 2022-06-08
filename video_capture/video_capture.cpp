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
//#include "liveMedia.hh"
#include "video_capture.h"
//#include "audio_capture.h"
#include "sample_comm.h"
#include "rtsp_demo.h"
#include "comm.h"
#include "mpi_region.h"
#include "pq_bin.h"
#include "gk_api_isp.h"
#include "gk_api_sys.h"

////int g_vid_shm = -1;
//struct video_shm_sync_st *g_p_v_shm = (video_shm_sync_st*)-1;//
////struct video_shm_sync_st *g_p_v_shm_tmp = (video_shm_sync_st*)-1;//
typedef struct {
  rtsp_demo_handle g_rtsplive = NULL;
  rtsp_session_handle session= NULL;
}rtsp_handle_struct;

#define BIG_STREAM_SIZE     PIC_2688x1944
#define SMALL_STREAM_SIZE   PIC_VGA

static pthread_t VencPid;


#define VB_MAX_NUM            10
#define ONLINE_LIMIT_WIDTH    2304

#define WRAP_BUF_LINE_EXT     416
rtsp_demo_handle p_g_rtsplive = NULL;
rtsp_session_handle p_session= NULL;

typedef struct hiSAMPLE_VPSS_ATTR_S
{
    SIZE_S            stMaxSize;
    DYNAMIC_RANGE_E   enDynamicRange;
    PIXEL_FORMAT_E    enPixelFormat;
    COMPRESS_MODE_E   enCompressMode[VPSS_MAX_PHY_CHN_NUM];
    SIZE_S            stOutPutSize[VPSS_MAX_PHY_CHN_NUM];
    FRAME_RATE_CTRL_S stFrameRate[VPSS_MAX_PHY_CHN_NUM];
    HI_BOOL           bMirror[VPSS_MAX_PHY_CHN_NUM];
    HI_BOOL           bFlip[VPSS_MAX_PHY_CHN_NUM];
    HI_BOOL           bChnEnable[VPSS_MAX_PHY_CHN_NUM];

    SAMPLE_SNS_TYPE_E enSnsType;
    HI_U32            BigStreamId;
    HI_U32            SmallStreamId;
    VI_VPSS_MODE_E    ViVpssMode;
    HI_BOOL           bWrapEn;
    HI_U32            WrapBufLine;
} SAMPLE_VPSS_CHN_ATTR_S;

typedef struct hiSAMPLE_VB_ATTR_S
{
    HI_U32            validNum;
    HI_U64            blkSize[VB_MAX_NUM];
    HI_U32            blkCnt[VB_MAX_NUM];
    HI_U32            supplementConfig;
} SAMPLE_VB_ATTR_S;

HI_S32 PutVideoStreamToRingBuffer(VENC_STREAM_S *pstStream);

/******************************************************************************
* function : show usage
******************************************************************************/
void SAMPLE_VENC_Usage(char* sPrgNm)
{
    printf("Usage : %s [index] \n", sPrgNm);
    printf("index:\n");
    printf("\t  0) H265(Large Stream)+H264(Small Stream)+JPEG lowdelay encode with RingBuf.\n");
    printf("\t  1) H.265e + H264e.\n");
    printf("\t  2) Qpmap:H.265e + H264e.\n");
    printf("\t  3) IntraRefresh:H.265e + H264e.\n");
    printf("\t  4) RoiBgFrameRate:H.265e + H.264e.\n");
    printf("\t  5) Mjpeg +Jpeg snap.\n");

    return;
}

/******************************************************************************
* function : to process abnormal case
******************************************************************************/
void SAMPLE_VENC_HandleSig(HI_S32 signo)
{
    signal(SIGINT, SIG_IGN);
    signal(SIGTERM, SIG_IGN);

    if (SIGINT == signo || SIGTERM == signo)
    {
        SAMPLE_COMM_VENC_StopSendQpmapFrame();
        SAMPLE_COMM_VENC_StopGetStream();
        SAMPLE_COMM_All_ISP_Stop();
        SAMPLE_COMM_SYS_Exit();
        printf("\033[0;31mprogram termination abnormally!\033[0;39m\n");
    }
    exit(-1);
}

/******************************************************************************
* function : to process abnormal case - the case of stream venc
******************************************************************************/
void SAMPLE_VENC_StreamHandleSig(HI_S32 signo)
{
    if (SIGINT == signo || SIGTERM == signo)
    {
        SAMPLE_COMM_SYS_Exit();
        printf("\033[0;31mprogram exit abnormally!\033[0;39m\n");
    }

    exit(0);
}

VENC_GOP_MODE_E SAMPLE_VENC_GetGopMode(void)
{
    char c;
    VENC_GOP_MODE_E enGopMode = (VENC_GOP_MODE_E)0;

Begin_Get:


    enGopMode = VENC_GOPMODE_NORMALP;
    return enGopMode;
}

SAMPLE_RC_E SAMPLE_VENC_GetRcMode(void)
{
    char c;
    SAMPLE_RC_E  enRcMode = (SAMPLE_RC_E)0;

Begin_Get:


  enRcMode = SAMPLE_RC_CBR;

  return enRcMode;
}

VENC_INTRA_REFRESH_MODE_E SAMPLE_VENC_GetIntraRefreshMode(void)
{
    char c;
    VENC_INTRA_REFRESH_MODE_E   enIntraRefreshMode = INTRA_REFRESH_ROW;

Begin_Get:

    printf("please input choose IntraRefresh mode!\n");
    printf("\t r) ROW.\n");
    printf("\t c) COLUMN.\n");

    while((c = getchar()) != '\n' && c != EOF)
    switch(c)
    {
        case 'r':
            enIntraRefreshMode = INTRA_REFRESH_ROW;
            break;
        case 'c':
            enIntraRefreshMode = INTRA_REFRESH_COLUMN;
            break;

        default:
            SAMPLE_PRT("input IntraRefresh Mode: %c, is invaild!\n",c);
            goto Begin_Get;
    }
    return enIntraRefreshMode;
}


static HI_U32 GetFrameRateFromSensorType(SAMPLE_SNS_TYPE_E enSnsType)
{
    HI_U32 FrameRate;

    SAMPLE_COMM_VI_GetFrameRateBySensor(enSnsType, &FrameRate);

    return FrameRate;
}

static HI_U32 GetFullLinesStdFromSensorType(SAMPLE_SNS_TYPE_E enSnsType)
{
    HI_U32 FullLinesStd = 0;

    switch (enSnsType)
    {
        case SONY_IMX327_MIPI_2M_30FPS_12BIT:
        case SONY_IMX327_MIPI_2M_30FPS_12BIT_WDR2TO1:
            FullLinesStd = 1125;
            break;
        case SONY_IMX307_MIPI_2M_30FPS_12BIT:
        case SONY_IMX307_MIPI_2M_30FPS_12BIT_WDR2TO1:
        case SONY_IMX307_2L_MIPI_2M_30FPS_12BIT:
        case SONY_IMX307_2L_MIPI_2M_30FPS_12BIT_WDR2TO1:
        case SMART_SC2235_DC_2M_30FPS_10BIT:
        case SMART_SC2231_MIPI_2M_30FPS_10BIT:
            FullLinesStd = 1125;
            break;
        case SONY_IMX335_MIPI_5M_30FPS_12BIT:
        case SONY_IMX335_MIPI_5M_30FPS_10BIT_WDR2TO1:
            FullLinesStd = 1875;
            break;
        case SONY_IMX335_MIPI_4M_30FPS_12BIT:
        case SONY_IMX335_MIPI_4M_30FPS_10BIT_WDR2TO1:
            FullLinesStd = 1375;
            break;
        case SMART_SC4236_MIPI_3M_30FPS_10BIT:
        case SMART_SC4236_MIPI_3M_20FPS_10BIT:
            FullLinesStd = 1600;
            break;
        case GALAXYCORE_GC2053_MIPI_2M_30FPS_10BIT:
        case GALAXYCORE_GC2053_MIPI_2M_30FPS_10BIT_FORCAR:
            FullLinesStd = 1108;
            break;
        case SMART_SC3235_MIPI_3M_30FPS_10BIT:
            FullLinesStd = 1350;                     /* 1350 sensor SC3235 full lines */
            break;
        case OMNIVISION_OS05A_MIPI_5M_30FPS_12BIT:
            SAMPLE_PRT("Error: sensor type %d resolution out of limits, not support ring!\n",enSnsType);
            break;
        default:
            SAMPLE_PRT("Error: Not support this sensor now! ==> %d\n",enSnsType);
            break;
    }

    return FullLinesStd;
}

static HI_VOID AdjustWrapBufLineBySnsType(SAMPLE_SNS_TYPE_E enSnsType, HI_U32 *pWrapBufLine)
{
    /*some sensor as follow need to expand the wrapBufLine*/
    if ((enSnsType == SMART_SC4236_MIPI_3M_30FPS_10BIT) ||
        (enSnsType == SMART_SC4236_MIPI_3M_20FPS_10BIT) ||
        (enSnsType == SMART_SC2235_DC_2M_30FPS_10BIT))
    {
        *pWrapBufLine += WRAP_BUF_LINE_EXT;
    }

    return;
}

static HI_VOID GetSensorResolution(SAMPLE_SNS_TYPE_E enSnsType, SIZE_S *pSnsSize)
{
    HI_S32          ret;
    SIZE_S          SnsSize;
    PIC_SIZE_E      enSnsSize;

    ret = SAMPLE_COMM_VI_GetSizeBySensor(enSnsType, &enSnsSize);
    if (HI_SUCCESS != ret)
    {
        SAMPLE_PRT("SAMPLE_COMM_VI_GetSizeBySensor failed!\n");
        return;
    }
    ret = SAMPLE_COMM_SYS_GetPicSize(enSnsSize, &SnsSize);
    if (HI_SUCCESS != ret)
    {
        SAMPLE_PRT("SAMPLE_COMM_SYS_GetPicSize failed!\n");
        return;
    }

    *pSnsSize = SnsSize;

    return;
}

static HI_VOID GetVpssWrapBufLine(SAMPLE_VPSS_CHN_ATTR_S *pParam)
{
    HI_U32 vpssWrapBufLine = 0;

    VPSS_VENC_WRAP_PARAM_S wrapParam;

    memset(&wrapParam, 0, sizeof(VPSS_VENC_WRAP_PARAM_S));
    wrapParam.bAllOnline      = (pParam->ViVpssMode == VI_ONLINE_VPSS_ONLINE) ? (GK_BOOL)1 : (GK_BOOL)0;
    wrapParam.u32FrameRate    = GetFrameRateFromSensorType(pParam->enSnsType);
    wrapParam.u32FullLinesStd = GetFullLinesStdFromSensorType(pParam->enSnsType);
    wrapParam.stLargeStreamSize.u32Width = pParam->stOutPutSize[pParam->BigStreamId].u32Width;
    wrapParam.stLargeStreamSize.u32Height= pParam->stOutPutSize[pParam->BigStreamId].u32Height;
    wrapParam.stSmallStreamSize.u32Width = pParam->stOutPutSize[pParam->SmallStreamId].u32Width;
    wrapParam.stSmallStreamSize.u32Height= pParam->stOutPutSize[pParam->SmallStreamId].u32Height;

    if (HI_MPI_SYS_GetVPSSVENCWrapBufferLine(&wrapParam, &vpssWrapBufLine) != HI_SUCCESS)
    {
        SAMPLE_PRT("Error:Current BigStream(%dx%d@%d fps) and SmallStream(%dx%d@%d fps) not support Ring!== return 0x%x(0x%x)\n",
            wrapParam.stLargeStreamSize.u32Width,wrapParam.stLargeStreamSize.u32Height,wrapParam.u32FrameRate,
            wrapParam.stSmallStreamSize.u32Width,wrapParam.stSmallStreamSize.u32Height,wrapParam.u32FrameRate,
            HI_MPI_SYS_GetVPSSVENCWrapBufferLine(&wrapParam, &vpssWrapBufLine), HI_ERR_SYS_NOT_SUPPORT);
        vpssWrapBufLine = 0;
    }
    else
    {
        AdjustWrapBufLineBySnsType(pParam->enSnsType, &vpssWrapBufLine);
    }

    pParam->WrapBufLine = vpssWrapBufLine;

    return;
}

static VI_VPSS_MODE_E GetViVpssModeFromResolution(SAMPLE_SNS_TYPE_E SnsType)
{
    SIZE_S SnsSize = {0};
    VI_VPSS_MODE_E ViVpssMode;

    GetSensorResolution(SnsType, &SnsSize);

    if (SnsSize.u32Width > ONLINE_LIMIT_WIDTH)
    {
        ViVpssMode = VI_OFFLINE_VPSS_ONLINE;
    }
    else
    {
        ViVpssMode = VI_ONLINE_VPSS_ONLINE;
    }

    return ViVpssMode;
}

static HI_VOID SAMPLE_VENC_GetDefaultVpssAttr(SAMPLE_SNS_TYPE_E enSnsType, HI_BOOL *pChanEnable, SIZE_S stEncSize[], SAMPLE_VPSS_CHN_ATTR_S *pVpssAttr)
{
    HI_S32 i;

    memset(pVpssAttr, 0, sizeof(SAMPLE_VPSS_CHN_ATTR_S));

    pVpssAttr->enDynamicRange = DYNAMIC_RANGE_SDR8;
    pVpssAttr->enPixelFormat  = PIXEL_FORMAT_YVU_SEMIPLANAR_420;
    pVpssAttr->bWrapEn        = (GK_BOOL)0;
    pVpssAttr->enSnsType      = enSnsType;
    pVpssAttr->ViVpssMode     = GetViVpssModeFromResolution(enSnsType);

    for (i = 0; i < VPSS_MAX_PHY_CHN_NUM; i++)
    {
        if (HI_TRUE == pChanEnable[i])
        {
            pVpssAttr->enCompressMode[i]          = (i == 0)? COMPRESS_MODE_SEG : COMPRESS_MODE_NONE;
            pVpssAttr->stOutPutSize[i].u32Width   = stEncSize[i].u32Width;
            pVpssAttr->stOutPutSize[i].u32Height  = stEncSize[i].u32Height;
            pVpssAttr->stFrameRate[i].s32SrcFrameRate  = -1;
            pVpssAttr->stFrameRate[i].s32DstFrameRate  = -1;
            pVpssAttr->bMirror[i]                      = HI_FALSE;
            pVpssAttr->bFlip[i]                        = HI_FALSE;

            pVpssAttr->bChnEnable[i]                   = HI_TRUE;
        }
    }

    return;
}

HI_S32 SAMPLE_VENC_SYS_Init(SAMPLE_VB_ATTR_S *pCommVbAttr)
{
    HI_S32 i;
    HI_S32 s32Ret;
    VB_CONFIG_S stVbConf;

    if (pCommVbAttr->validNum > VB_MAX_COMM_POOLS)
    {
        SAMPLE_PRT("SAMPLE_VENC_SYS_Init validNum(%d) too large than VB_MAX_COMM_POOLS(%d)!\n", pCommVbAttr->validNum, VB_MAX_COMM_POOLS);
        return HI_FAILURE;
    }

    memset(&stVbConf, 0, sizeof(VB_CONFIG_S));

    for (i = 0; i < pCommVbAttr->validNum; i++)
    {
        stVbConf.astCommPool[i].u64BlkSize   = pCommVbAttr->blkSize[i];
        stVbConf.astCommPool[i].u32BlkCnt    = pCommVbAttr->blkCnt[i];
        //printf("%s,%d,stVbConf.astCommPool[%d].u64BlkSize = %lld, blkSize = %d\n",__func__,__LINE__,i,stVbConf.astCommPool[i].u64BlkSize,stVbConf.astCommPool[i].u32BlkCnt);
    }

    stVbConf.u32MaxPoolCnt = pCommVbAttr->validNum;

    if(pCommVbAttr->supplementConfig == 0)
    {
        s32Ret = SAMPLE_COMM_SYS_Init(&stVbConf);
    }
    else
    {
        s32Ret = SAMPLE_COMM_SYS_InitWithVbSupplement(&stVbConf,pCommVbAttr->supplementConfig);
    }

    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("SAMPLE_COMM_SYS_GetPicSize failed!\n");
        return s32Ret;
    }

    return HI_SUCCESS;
}

HI_VOID SAMPLE_VENC_SetDCFInfo(VI_PIPE ViPipe)
{
    ISP_DCF_INFO_S stIspDCF;

    HI_MPI_ISP_GetDCFInfo(ViPipe, &stIspDCF);

    strncpy((char *)stIspDCF.stIspDCFConstInfo.au8ImageDescription, "Thumbnail test", DCF_DRSCRIPTION_LENGTH);
    strncpy((char *)stIspDCF.stIspDCFConstInfo.au8Make, "Hisilicon", DCF_DRSCRIPTION_LENGTH);
    strncpy((char *)stIspDCF.stIspDCFConstInfo.au8Model, "IP Camera", DCF_DRSCRIPTION_LENGTH);
    strncpy((char *)stIspDCF.stIspDCFConstInfo.au8Software, "v.1.1.0", DCF_DRSCRIPTION_LENGTH);

    stIspDCF.stIspDCFConstInfo.u32FocalLength             = 0x00640001;
    stIspDCF.stIspDCFConstInfo.u8Contrast                 = 5;
    stIspDCF.stIspDCFConstInfo.u8CustomRendered           = 0;
    stIspDCF.stIspDCFConstInfo.u8FocalLengthIn35mmFilm    = 1;
    stIspDCF.stIspDCFConstInfo.u8GainControl              = 1;
    stIspDCF.stIspDCFConstInfo.u8LightSource              = 1;
    stIspDCF.stIspDCFConstInfo.u8MeteringMode             = 1;
    stIspDCF.stIspDCFConstInfo.u8Saturation               = 1;
    stIspDCF.stIspDCFConstInfo.u8SceneCaptureType         = 1;
    stIspDCF.stIspDCFConstInfo.u8SceneType                = 0;
    stIspDCF.stIspDCFConstInfo.u8Sharpness                = 5;
    stIspDCF.stIspDCFUpdateInfo.u32ISOSpeedRatings         = 500;
    stIspDCF.stIspDCFUpdateInfo.u32ExposureBiasValue       = 5;
    stIspDCF.stIspDCFUpdateInfo.u32ExposureTime            = 0x00010004;
    stIspDCF.stIspDCFUpdateInfo.u32FNumber                 = 0x0001000f;
    stIspDCF.stIspDCFUpdateInfo.u8WhiteBalance             = 1;
    stIspDCF.stIspDCFUpdateInfo.u8ExposureMode             = 0;
    stIspDCF.stIspDCFUpdateInfo.u8ExposureProgram          = 1;
    stIspDCF.stIspDCFUpdateInfo.u32MaxApertureValue        = 0x00010001;

    HI_MPI_ISP_SetDCFInfo(ViPipe, &stIspDCF);

    return;
}

HI_S32 SAMPLE_VENC_VI_Init( SAMPLE_VI_CONFIG_S *pstViConfig, VI_VPSS_MODE_E ViVpssMode)
{
    HI_S32              s32Ret;
    SAMPLE_SNS_TYPE_E   enSnsType;
    ISP_CTRL_PARAM_S    stIspCtrlParam;
    HI_U32              u32FrameRate;


    enSnsType = pstViConfig->astViInfo[0].stSnsInfo.enSnsType;

    pstViConfig->as32WorkingViId[0]                           = 0;

    pstViConfig->astViInfo[0].stSnsInfo.MipiDev            = SAMPLE_COMM_VI_GetComboDevBySensor(pstViConfig->astViInfo[0].stSnsInfo.enSnsType, 0);
    pstViConfig->astViInfo[0].stSnsInfo.s32BusId           = 0;
    pstViConfig->astViInfo[0].stDevInfo.enWDRMode          = WDR_MODE_NONE;
    pstViConfig->astViInfo[0].stPipeInfo.enMastPipeMode    = ViVpssMode;

    //pstViConfig->astViInfo[0].stPipeInfo.aPipe[0]          = ViPipe0;
    pstViConfig->astViInfo[0].stPipeInfo.aPipe[1]          = -1;

    //pstViConfig->astViInfo[0].stChnInfo.ViChn              = ViChn;
    //pstViConfig->astViInfo[0].stChnInfo.enPixFormat        = PIXEL_FORMAT_YVU_SEMIPLANAR_420;
    //pstViConfig->astViInfo[0].stChnInfo.enDynamicRange     = enDynamicRange;
    pstViConfig->astViInfo[0].stChnInfo.enVideoFormat      = VIDEO_FORMAT_LINEAR;
    pstViConfig->astViInfo[0].stChnInfo.enCompressMode     = COMPRESS_MODE_NONE;
    s32Ret = SAMPLE_COMM_VI_SetParam(pstViConfig);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("SAMPLE_COMM_VI_SetParam failed with %d!\n", s32Ret);
        return s32Ret;
    }

    SAMPLE_COMM_VI_GetFrameRateBySensor(enSnsType, &u32FrameRate);

    s32Ret = HI_MPI_ISP_GetCtrlParam(pstViConfig->astViInfo[0].stPipeInfo.aPipe[0], &stIspCtrlParam);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("HI_MPI_ISP_GetCtrlParam failed with %d!\n", s32Ret);
        return s32Ret;
    }
    stIspCtrlParam.u32StatIntvl  = u32FrameRate/30;
    if (stIspCtrlParam.u32StatIntvl == 0)
    {
        stIspCtrlParam.u32StatIntvl = 1;
    }

    s32Ret = HI_MPI_ISP_SetCtrlParam(pstViConfig->astViInfo[0].stPipeInfo.aPipe[0], &stIspCtrlParam);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("HI_MPI_ISP_SetCtrlParam failed with %d!\n", s32Ret);
        return s32Ret;
    }

    s32Ret = SAMPLE_COMM_VI_StartVi(pstViConfig);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_COMM_SYS_Exit();
        SAMPLE_PRT("SAMPLE_COMM_VI_StartVi failed with %d!\n", s32Ret);
        return s32Ret;
    }

    return HI_SUCCESS;
}

static HI_S32 SAMPLE_VENC_VPSS_CreateGrp(VPSS_GRP VpssGrp, SAMPLE_VPSS_CHN_ATTR_S *pParam)
{
    HI_S32          s32Ret;
    PIC_SIZE_E      enSnsSize;
    SIZE_S          stSnsSize;
    VPSS_GRP_ATTR_S stVpssGrpAttr = {0};

    s32Ret = SAMPLE_COMM_VI_GetSizeBySensor(pParam->enSnsType, &enSnsSize);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("SAMPLE_COMM_VI_GetSizeBySensor failed!\n");
        return s32Ret;
    }

    s32Ret = SAMPLE_COMM_SYS_GetPicSize(enSnsSize, &stSnsSize);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("SAMPLE_COMM_SYS_GetPicSize failed!\n");
        return s32Ret;
    }

    stVpssGrpAttr.enDynamicRange          = pParam->enDynamicRange;
    stVpssGrpAttr.enPixelFormat           = pParam->enPixelFormat;
    stVpssGrpAttr.u32MaxW                 = stSnsSize.u32Width;
    stVpssGrpAttr.u32MaxH                 = stSnsSize.u32Height;
    stVpssGrpAttr.bNrEn                   = HI_TRUE;
    stVpssGrpAttr.stNrAttr.enNrType       = VPSS_NR_TYPE_VIDEO;
    stVpssGrpAttr.stNrAttr.enNrMotionMode = NR_MOTION_MODE_NORMAL;
    stVpssGrpAttr.stNrAttr.enCompressMode = COMPRESS_MODE_FRAME;
    stVpssGrpAttr.stFrameRate.s32SrcFrameRate = -1;
    stVpssGrpAttr.stFrameRate.s32DstFrameRate = -1;

    s32Ret = HI_MPI_VPSS_CreateGrp(VpssGrp, &stVpssGrpAttr);

    if (s32Ret != HI_SUCCESS)
    {
        SAMPLE_PRT("HI_MPI_VPSS_CreateGrp(grp:%d) failed with %#x!\n", VpssGrp, s32Ret);
        return HI_FAILURE;
    }

    return s32Ret;
}

static HI_S32 SAMPLE_VENC_VPSS_DestoryGrp(VPSS_GRP VpssGrp)
{
    HI_S32          s32Ret;

    s32Ret = HI_MPI_VPSS_DestroyGrp(VpssGrp);
    if (s32Ret != HI_SUCCESS)
    {
        SAMPLE_PRT("failed with %#x!\n", s32Ret);
        return HI_FAILURE;
    }

    return s32Ret;
}

static HI_S32 SAMPLE_VENC_VPSS_StartGrp(VPSS_GRP VpssGrp)
{
    HI_S32          s32Ret;

    s32Ret = HI_MPI_VPSS_StartGrp(VpssGrp);
    if (s32Ret != HI_SUCCESS)
    {
        SAMPLE_PRT("HI_MPI_VPSS_CreateGrp(grp:%d) failed with %#x!\n", VpssGrp, s32Ret);
        return HI_FAILURE;
    }

    return s32Ret;
}

static HI_S32 SAMPLE_VENC_VPSS_ChnEnable(VPSS_GRP VpssGrp, VPSS_CHN VpssChn, SAMPLE_VPSS_CHN_ATTR_S *pParam, HI_BOOL bWrapEn)
{
    HI_S32 s32Ret;
    VPSS_CHN_ATTR_S     stVpssChnAttr;
    VPSS_CHN_BUF_WRAP_S stVpssChnBufWrap;

    memset(&stVpssChnAttr, 0, sizeof(VPSS_CHN_ATTR_S));
    stVpssChnAttr.u32Width                     = pParam->stOutPutSize[VpssChn].u32Width;
    stVpssChnAttr.u32Height                    = pParam->stOutPutSize[VpssChn].u32Height;
    stVpssChnAttr.enChnMode                    = VPSS_CHN_MODE_USER;
    stVpssChnAttr.enCompressMode               = pParam->enCompressMode[VpssChn];
    stVpssChnAttr.enDynamicRange               = pParam->enDynamicRange;
    stVpssChnAttr.enPixelFormat                = pParam->enPixelFormat;
    if (stVpssChnAttr.u32Width * stVpssChnAttr.u32Height > 2688 * 1520 ) {
        stVpssChnAttr.stFrameRate.s32SrcFrameRate  = 30;
        stVpssChnAttr.stFrameRate.s32DstFrameRate  = 20;
    } else {
        stVpssChnAttr.stFrameRate.s32SrcFrameRate  = pParam->stFrameRate[VpssChn].s32SrcFrameRate;
        stVpssChnAttr.stFrameRate.s32DstFrameRate  = pParam->stFrameRate[VpssChn].s32DstFrameRate;
    }
    stVpssChnAttr.u32Depth                     = 0;
    stVpssChnAttr.bMirror                      = pParam->bMirror[VpssChn];
    stVpssChnAttr.bFlip                        = pParam->bFlip[VpssChn];
    stVpssChnAttr.enVideoFormat                = VIDEO_FORMAT_LINEAR;
    stVpssChnAttr.stAspectRatio.enMode         = ASPECT_RATIO_NONE;

    s32Ret = HI_MPI_VPSS_SetChnAttr(VpssGrp, VpssChn, &stVpssChnAttr);
    if (s32Ret != HI_SUCCESS)
    {
        SAMPLE_PRT("HI_MPI_VPSS_SetChnAttr chan %d failed with %#x\n", VpssChn, s32Ret);
        goto exit0;
    }

    if (bWrapEn)
    {
        if (VpssChn != 0)   //vpss limit! just vpss chan0 support wrap
        {
            SAMPLE_PRT("Error:Just vpss chan 0 support wrap! Current chan %d\n", VpssChn);
            goto exit0;
        }


        HI_U32 WrapBufLen = 0;
        VPSS_VENC_WRAP_PARAM_S WrapParam;

        memset(&WrapParam, 0, sizeof(VPSS_VENC_WRAP_PARAM_S));
        WrapParam.bAllOnline      = (pParam->ViVpssMode == VI_ONLINE_VPSS_ONLINE) ? (GK_BOOL)1 : (GK_BOOL)0;
        WrapParam.u32FrameRate    = GetFrameRateFromSensorType(pParam->enSnsType);
        WrapParam.u32FullLinesStd = GetFullLinesStdFromSensorType(pParam->enSnsType);
        WrapParam.stLargeStreamSize.u32Width = pParam->stOutPutSize[pParam->BigStreamId].u32Width;
        WrapParam.stLargeStreamSize.u32Height= pParam->stOutPutSize[pParam->BigStreamId].u32Height;
        WrapParam.stSmallStreamSize.u32Width = pParam->stOutPutSize[pParam->SmallStreamId].u32Width;
        WrapParam.stSmallStreamSize.u32Height= pParam->stOutPutSize[pParam->SmallStreamId].u32Height;

        if (HI_MPI_SYS_GetVPSSVENCWrapBufferLine(&WrapParam, &WrapBufLen) == HI_SUCCESS)
        {
            AdjustWrapBufLineBySnsType(pParam->enSnsType, &WrapBufLen);

            stVpssChnBufWrap.u32WrapBufferSize = VPSS_GetWrapBufferSize(WrapParam.stLargeStreamSize.u32Width,
                WrapParam.stLargeStreamSize.u32Height, WrapBufLen, pParam->enPixelFormat, DATA_BITWIDTH_8,
                COMPRESS_MODE_NONE, DEFAULT_ALIGN);
            stVpssChnBufWrap.bEnable = HI_TRUE;
            stVpssChnBufWrap.u32BufLine = WrapBufLen;
            s32Ret = HI_MPI_VPSS_SetChnBufWrapAttr(VpssGrp, VpssChn, &stVpssChnBufWrap);
            if (s32Ret != HI_SUCCESS)
            {
                SAMPLE_PRT("HI_MPI_VPSS_SetChnBufWrapAttr Chn %d failed with %#x\n", VpssChn, s32Ret);
                goto exit0;
            }
        }
        else
        {
            SAMPLE_PRT("Current sensor type: %d, not support BigStream(%dx%d) and SmallStream(%dx%d) Ring!!\n",
                pParam->enSnsType,
                pParam->stOutPutSize[pParam->BigStreamId].u32Width, pParam->stOutPutSize[pParam->BigStreamId].u32Height,
                pParam->stOutPutSize[pParam->SmallStreamId].u32Width, pParam->stOutPutSize[pParam->SmallStreamId].u32Height);
        }

    }

    s32Ret = HI_MPI_VPSS_EnableChn(VpssGrp, VpssChn);
    if (s32Ret != HI_SUCCESS)
    {
        SAMPLE_PRT("HI_MPI_VPSS_EnableChn (%d) failed with %#x\n", VpssChn, s32Ret);
        goto exit0;
    }

exit0:
    return s32Ret;
}

static HI_S32 SAMPLE_VENC_VPSS_ChnDisable(VPSS_GRP VpssGrp, VPSS_CHN VpssChn)
{
    HI_S32 s32Ret;

    s32Ret = HI_MPI_VPSS_DisableChn(VpssGrp, VpssChn);

    if (s32Ret != HI_SUCCESS)
    {
        SAMPLE_PRT("failed with %#x!\n", s32Ret);
        return HI_FAILURE;
    }

    return s32Ret;
}

static HI_S32 SAMPLE_VENC_VPSS_Init(VPSS_GRP VpssGrp, SAMPLE_VPSS_CHN_ATTR_S *pstParam)
{
    HI_S32 i,j;
    HI_S32 s32Ret;
    HI_BOOL bWrapEn;

    s32Ret = SAMPLE_VENC_VPSS_CreateGrp(VpssGrp, pstParam);
    if (s32Ret != HI_SUCCESS)
    {
        goto exit0;
    }

    for (i = 0; i < VPSS_MAX_PHY_CHN_NUM; i++)
    {
        if (pstParam->bChnEnable[i] == HI_TRUE)
        {
            bWrapEn = (i==0)? pstParam->bWrapEn : (GK_BOOL)0;

            s32Ret = SAMPLE_VENC_VPSS_ChnEnable(VpssGrp, i, pstParam, bWrapEn);
            if (s32Ret != HI_SUCCESS)
            {
                goto exit1;
            }
        }
    }

    i--; // for abnormal case 'exit1' prossess;

    s32Ret = SAMPLE_VENC_VPSS_StartGrp(VpssGrp);
    if (s32Ret != HI_SUCCESS)
    {
        goto exit1;
    }

    return s32Ret;

exit1:
    for (j = 0; j <= i; j++)
    {
        if (pstParam->bChnEnable[j] == HI_TRUE)
        {
            SAMPLE_VENC_VPSS_ChnDisable(VpssGrp, i);
        }
    }

    SAMPLE_VENC_VPSS_DestoryGrp(VpssGrp);
exit0:
    return s32Ret;
}

static HI_VOID SAMPLE_VENC_GetCommVbAttr(const SAMPLE_SNS_TYPE_E enSnsType, const SAMPLE_VPSS_CHN_ATTR_S *pstParam,
    HI_BOOL bSupportDcf, SAMPLE_VB_ATTR_S * pstcommVbAttr)
{
    if (pstParam->ViVpssMode != VI_ONLINE_VPSS_ONLINE)
    {
        SIZE_S snsSize = {0};
        GetSensorResolution(enSnsType, &snsSize);

        if (pstParam->ViVpssMode == VI_OFFLINE_VPSS_ONLINE || pstParam->ViVpssMode == VI_OFFLINE_VPSS_OFFLINE)
        {
            pstcommVbAttr->blkSize[pstcommVbAttr->validNum] = VI_GetRawBufferSize(snsSize.u32Width, snsSize.u32Height,
                                                                                  PIXEL_FORMAT_RGB_BAYER_12BPP,
                                                                                  COMPRESS_MODE_NONE,
                                                                                  DEFAULT_ALIGN);
            pstcommVbAttr->blkCnt[pstcommVbAttr->validNum]  = 3;
            pstcommVbAttr->validNum++;
        }

        if (pstParam->ViVpssMode == VI_OFFLINE_VPSS_OFFLINE)
        {
            pstcommVbAttr->blkSize[pstcommVbAttr->validNum] = COMMON_GetPicBufferSize(snsSize.u32Width, snsSize.u32Height,
                                                                                      PIXEL_FORMAT_YVU_SEMIPLANAR_420,
                                                                                      DATA_BITWIDTH_8,
                                                                                      COMPRESS_MODE_NONE,
                                                                                      DEFAULT_ALIGN);
            pstcommVbAttr->blkCnt[pstcommVbAttr->validNum]  = 2;
            pstcommVbAttr->validNum++;
        }

        if (pstParam->ViVpssMode == VI_ONLINE_VPSS_OFFLINE)
        {
            pstcommVbAttr->blkSize[pstcommVbAttr->validNum] = COMMON_GetPicBufferSize(snsSize.u32Width, snsSize.u32Height,
                                                                                      PIXEL_FORMAT_YVU_SEMIPLANAR_420,
                                                                                      DATA_BITWIDTH_8,
                                                                                      COMPRESS_MODE_NONE,
                                                                                      DEFAULT_ALIGN);
            pstcommVbAttr->blkCnt[pstcommVbAttr->validNum]  = 3;
            pstcommVbAttr->validNum++;

        }
    }
    if(HI_TRUE == pstParam->bWrapEn)
    {
        pstcommVbAttr->blkSize[pstcommVbAttr->validNum] = VPSS_GetWrapBufferSize(pstParam->stOutPutSize[pstParam->BigStreamId].u32Width,
                                                                                 pstParam->stOutPutSize[pstParam->BigStreamId].u32Height,
                                                                                 pstParam->WrapBufLine,
                                                                                 pstParam->enPixelFormat,DATA_BITWIDTH_8,COMPRESS_MODE_NONE,DEFAULT_ALIGN);
        pstcommVbAttr->blkCnt[pstcommVbAttr->validNum]  = 1;
        pstcommVbAttr->validNum++;
    }
    else
    {
        pstcommVbAttr->blkSize[pstcommVbAttr->validNum] = COMMON_GetPicBufferSize(pstParam->stOutPutSize[0].u32Width, pstParam->stOutPutSize[0].u32Height,
                                                                                  pstParam->enPixelFormat,
                                                                                  DATA_BITWIDTH_8,
                                                                                  pstParam->enCompressMode[0],
                                                                                  DEFAULT_ALIGN);

        if (pstParam->ViVpssMode == VI_ONLINE_VPSS_ONLINE)
        {
            pstcommVbAttr->blkCnt[pstcommVbAttr->validNum]  = 3;
        }
        else
        {
            pstcommVbAttr->blkCnt[pstcommVbAttr->validNum]  = 2;
        }

        pstcommVbAttr->validNum++;
    }



    pstcommVbAttr->blkSize[pstcommVbAttr->validNum] = COMMON_GetPicBufferSize(pstParam->stOutPutSize[1].u32Width, pstParam->stOutPutSize[1].u32Height,
                                                                              pstParam->enPixelFormat,
                                                                              DATA_BITWIDTH_8,
                                                                              pstParam->enCompressMode[1],
                                                                              DEFAULT_ALIGN);

    if (pstParam->ViVpssMode == VI_ONLINE_VPSS_ONLINE)
    {
        pstcommVbAttr->blkCnt[pstcommVbAttr->validNum]  = 3;
    }
    else
    {
        pstcommVbAttr->blkCnt[pstcommVbAttr->validNum]  = 2;
    }
    pstcommVbAttr->validNum++;


    //vgs dcf use
    if(HI_TRUE == bSupportDcf)
    {
        pstcommVbAttr->blkSize[pstcommVbAttr->validNum] = COMMON_GetPicBufferSize(160, 120,
                                                                                  pstParam->enPixelFormat,
                                                                                  DATA_BITWIDTH_8,
                                                                                  COMPRESS_MODE_NONE,
                                                                                  DEFAULT_ALIGN);
        pstcommVbAttr->blkCnt[pstcommVbAttr->validNum]  = 1;
        pstcommVbAttr->validNum++;
    }

}

HI_S32 SAMPLE_VENC_CheckSensor(SAMPLE_SNS_TYPE_E   enSnsType,SIZE_S  stSize)
{
    HI_S32 s32Ret;
    SIZE_S          stSnsSize;
    PIC_SIZE_E      enSnsSize;

    s32Ret = SAMPLE_COMM_VI_GetSizeBySensor(enSnsType, &enSnsSize);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("SAMPLE_COMM_VI_GetSizeBySensor failed!\n");
        return s32Ret;
    }
    s32Ret = SAMPLE_COMM_SYS_GetPicSize(enSnsSize, &stSnsSize);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("SAMPLE_COMM_SYS_GetPicSize failed!\n");
        return s32Ret;
    }

    if((stSnsSize.u32Width < stSize.u32Width) || (stSnsSize.u32Height < stSize.u32Height))
    {
        //SAMPLE_PRT("Sensor size is (%d,%d), but encode chnl is (%d,%d) !\n",
            //stSnsSize.u32Width,stSnsSize.u32Height,stSize.u32Width,stSize.u32Height);
        return HI_FAILURE;
    }

    return HI_SUCCESS;
}

HI_S32 SAMPLE_VENC_ModifyResolution(SAMPLE_SNS_TYPE_E   enSnsType,PIC_SIZE_E *penSize,SIZE_S *pstSize)
{
    HI_S32 s32Ret;
    SIZE_S          stSnsSize;
    PIC_SIZE_E      enSnsSize;

    s32Ret = SAMPLE_COMM_VI_GetSizeBySensor(enSnsType, &enSnsSize);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("SAMPLE_COMM_VI_GetSizeBySensor failed!\n");
        return s32Ret;
    }
    s32Ret = SAMPLE_COMM_SYS_GetPicSize(enSnsSize, &stSnsSize);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("SAMPLE_COMM_SYS_GetPicSize failed!\n");
        return s32Ret;
    }

    *penSize = enSnsSize;
    pstSize->u32Width  = stSnsSize.u32Width;
    pstSize->u32Height = stSnsSize.u32Height;

    return HI_SUCCESS;
}

/******************************************************************************
* funciton : get stream from each channels and save them
******************************************************************************/
HI_VOID* VENC_GetVencStreamProc(HI_VOID *p)
{  
	HI_S32 s32Ret = 0;
	static int s_LivevencChn = 0,s_LivevencFd=0; 
	static int s_maxFd = 0;

	fd_set read_fds;
	VENC_STREAM_S stVStream;
	VENC_CHN_STATUS_S stStat;

	s_LivevencChn = 0;
	s_LivevencFd  = HI_MPI_VENC_GetFd(s_LivevencChn);	
	s_maxFd   = s_maxFd > s_LivevencFd ? s_maxFd:s_LivevencFd;	
	s_maxFd = s_maxFd+1;

	pthread_detach(pthread_self());	
	//struct sched_param param;
	struct timeval TimeoutVal;
	VENC_PACK_S *pstPack = NULL;	
	pstPack = (VENC_PACK_S *)malloc(sizeof(VENC_PACK_S) * 128);
	printf("enter streamproc function...\n");
          SAMPLE_PRT("before  steamproc function...!!\n");
	if (NULL == pstPack)
	{
		pstPack = NULL;
		return NULL;
	}	
	int i;

	unsigned char* pStremData = NULL;
	int nSize = 0;
	printf("enter streamproc function2...\n");

	while(1)	
	{
		FD_ZERO( &read_fds );
		FD_SET( s_LivevencFd, &read_fds);
		
		TimeoutVal.tv_sec  = 2;
		TimeoutVal.tv_usec = 0;
		s32Ret = select( s_maxFd, &read_fds, NULL, NULL, &TimeoutVal );
		if (s32Ret <= 0)
		{
			printf("%s select failed!\n",__FUNCTION__);
			sleep(1);
			continue;
		}	

		//Live stream
		if (FD_ISSET( s_LivevencFd, &read_fds ))
		{
			s32Ret = HI_MPI_VENC_QueryStatus( s_LivevencChn, &stStat );
			if (HI_SUCCESS != s32Ret)
			{
				printf("HI_MPI_VENC_Query chn[%d] failed with %#x!\n", s_LivevencChn, s32Ret);
				continue;
			}
	
			stVStream.pstPack = pstPack;
			stVStream.u32PackCount = stStat.u32CurPacks;
			s32Ret = HI_MPI_VENC_GetStream( s_LivevencChn, &stVStream, HI_TRUE );
			if (HI_SUCCESS != s32Ret)
			{
				printf("HI_MPI_VENC_GetStream .. failed with %#x!\n", s32Ret);
				continue;
			}
	
	
			for (i = 0; i < stVStream.u32PackCount; i++)
			{
				//????SEI?
				//if(stVStream.pstPack[i].DataType.enH265EType == H265E_NALU_SEI) continue;				

				pStremData = (unsigned char*)stVStream.pstPack[i].pu8Addr+stVStream.pstPack[i].u32Offset;
				nSize = stVStream.pstPack[i].u32Len-stVStream.pstPack[i].u32Offset;

				if(p_g_rtsplive)
				{
					rtsp_sever_tx_video(p_g_rtsplive,p_session,pStremData,nSize,stVStream.pstPack[i].u64PTS);
				}
			}	
	
			s32Ret = HI_MPI_VENC_ReleaseStream(s_LivevencChn, &stVStream);
			if (HI_SUCCESS != s32Ret)
			{
				SAMPLE_PRT("HI_MPI_VENC_ReleaseStream chn[%d] .. failed with %#x!\n", s_LivevencChn, s32Ret);
				stVStream.pstPack = NULL;
				continue;
			}
		} 
	}
	
	if(pstPack) free(pstPack);
	return NULL;
}

int OSD_Handle_Init( RGN_HANDLE RgnHandle, VENC_CHN RgnVencChn)
{
	HI_S32 s32Ret = HI_FAILURE;
	RGN_ATTR_S stRgnAttr;
	MPP_CHN_S stChn;
	VENC_CHN VencGrp;
	RGN_CHN_ATTR_S stChnAttr;

	/******************************************
	*  step 1: create overlay regions
	*****************************************/
	stRgnAttr.enType                            = OVERLAYEX_RGN; //region type.
	stRgnAttr.unAttr.stOverlay.enPixelFmt 		= PIXEL_FORMAT_ARGB_1555; //format.
	stRgnAttr.unAttr.stOverlay.stSize.u32Width  = 0;
	stRgnAttr.unAttr.stOverlay.stSize.u32Height = 0;
	stRgnAttr.unAttr.stOverlay.u32BgColor = 0;
	printf("whs:enter osd_handle_init...\n");

	s32Ret = HI_MPI_RGN_Create(RgnHandle, &stRgnAttr);
	if (HI_SUCCESS != s32Ret)
	{
		SAMPLE_PRT("HI_MPI_RGN_Create (%d) failed with %#x!\n", RgnHandle, s32Ret);
		return HI_FAILURE;
	}
	SAMPLE_PRT("create handle:%d success!\n", RgnHandle);
	printf("create handle:%d success!\n", RgnHandle);

	/***********************************************************
	* step 2: attach created region handle to venc channel.
	**********************************************************/
	VencGrp = RgnVencChn;
	stChn.enModId  = HI_ID_VENC;
	stChn.s32DevId = 0;//0;
	stChn.s32ChnId = RgnVencChn;
	memset(&stChnAttr, 0, sizeof(stChnAttr));
	stChnAttr.bShow 	= HI_TRUE;
	stChnAttr.enType 	= OVERLAYEX_RGN ;
	stChnAttr.unChnAttr.stOverlayChn.stPoint.s32X    = 0;
	stChnAttr.unChnAttr.stOverlayChn.stPoint.s32Y    = 0;           
	stChnAttr.unChnAttr.stOverlayChn.u32BgAlpha      = 0;
    stChnAttr.unChnAttr.stOverlayChn.u32FgAlpha 	 = 0;
    stChnAttr.unChnAttr.stOverlayChn.u32Layer 	     = 0;
	stChnAttr.unChnAttr.stOverlayChn.stQpInfo.bAbsQp = HI_FALSE;
	stChnAttr.unChnAttr.stOverlayChn.stQpInfo.s32Qp  = 0;
	s32Ret = HI_MPI_RGN_AttachToChn(RgnHandle, &stChn, &stChnAttr);
	if (HI_SUCCESS != s32Ret)
	{
		SAMPLE_PRT("HI_MPI_RGN_AttachToChn (%d to %d) failed with %#x!\n", RgnHandle, VencGrp, s32Ret);
		return HI_FAILURE;
	}
	return HI_SUCCESS;
}
/* 
    hisi ap:SAMPLE_BIN_IMPORTBINDATA
    editor:whs
 */
static GK_S32 SAMPILE_BIN_ImportBinData(PQ_BIN_MODULE_S *pstBinParam, unsigned char *pBuffer, unsigned int dataLen)
{
    unsigned int tempLen;
    unsigned int size;
    int ret;

    FILE *pFile = fopen("/ko/pq_bin_gc2053.bin", "r");
    if (pFile == NULL) {
        printf("fopen error.\n");
	return -1;
    }
    fseek(pFile, 0, SEEK_END);
    size = ftell(pFile);
    fseek(pFile, 0, SEEK_SET);

    unsigned char *pDataBuffer = (unsigned char *)malloc(size);
    if (pDataBuffer == GK_NULL) {
        printf("pDataBuffer malloc  fail\n");
        return GK_FAILURE;
    }
    tempLen = fread(pDataBuffer, sizeof(unsigned char), size, pFile);
    if (tempLen == 0) {
        printf("read erro\n");
        ret = -1;
        goto EXIT;
    }
    ret = PQ_BIN_ImportBinData(pstBinParam, pDataBuffer, size);
    if (ret != 0) {
        printf("PQ_BIN_ImportBinData error! errno(%#x)\n", ret);
    } else {
        printf("PQ_BIN_ParseBinData success!\n");
    }

EXIT:
    free(pDataBuffer);
    pDataBuffer = NULL;
    if (pFile != NULL) {
        fclose(pFile);
    }

    return ret;
}
/* 
    load_pq file function
    editor:whs
 */
int load_pq_bin() {
    int ret;
    unsigned int totalLen, ispDataLen, nrxDataLen;
    PQ_BIN_MODULE_S stBinParam;

    stBinParam.stISP.enable = 1;
    stBinParam.st3DNR.enable = 0;
    stBinParam.st3DNR.viPipe = 0;
    stBinParam.st3DNR.vpssGrp = 0;
    ispDataLen = PQ_GetISPDataTotalLen();
    nrxDataLen = PQ_GetStructParamLen(&stBinParam);
    totalLen = nrxDataLen + ispDataLen;
    unsigned char *pBuffer = (unsigned char *)malloc(totalLen);
    if (pBuffer == NULL) {
        printf("malloc err!\n");
        return -1;
    }
    memset_s(pBuffer, totalLen, 0, totalLen);
    ret = SAMPILE_BIN_ImportBinData(&stBinParam, pBuffer, totalLen);
    free(pBuffer);
    pBuffer = NULL;
    return ret;
}

/******************************************************************************
* function: H.265e + H264e@720P, H.265 Channel resolution adaptable with sensor
******************************************************************************/
void *thVideoCapture(void *arg)
{
#if 1
      HI_S32 i;
      HI_S32 s32Ret;
      SIZE_S          stSize[2];
      PIC_SIZE_E      enSize[2]     = {BIG_STREAM_SIZE,SMALL_STREAM_SIZE};
      HI_S32          s32ChnNum     = 2;
      VENC_CHN        VencChn[2]    = {0,1};
      HI_U32          u32Profile[2] = {0,0};
      PAYLOAD_TYPE_E  enPayLoad[2]  = {PT_H265,PT_H264};
      VENC_GOP_MODE_E enGopMode;
      VENC_GOP_ATTR_S stGopAttr;
      SAMPLE_RC_E     enRcMode;
      HI_BOOL         bRcnRefShareBuf = HI_TRUE;
  
      VI_DEV          ViDev        = 0;
      VI_PIPE         ViPipe       = 0;
      VI_CHN          ViChn        = 0;
      SAMPLE_VI_CONFIG_S stViConfig;
  
      VPSS_GRP        VpssGrp        = 0;
      VPSS_CHN        VpssChn[2]     = {0,1};
      HI_BOOL         abChnEnable[VPSS_MAX_PHY_CHN_NUM] = {HI_TRUE,HI_TRUE,HI_FALSE};
      SAMPLE_VPSS_CHN_ATTR_S stParam;
      SAMPLE_VB_ATTR_S commVbAttr;
  //g_p_v_shm = (video_shm_sync_st*)arg;

      rtsp_handle_struct* rtsp_handle = (rtsp_handle_struct*)arg;
  	p_g_rtsplive = rtsp_handle->g_rtsplive;
	p_session= rtsp_handle->session;
    
      for(i=0; i<s32ChnNum; i++)
      {
          s32Ret = SAMPLE_COMM_SYS_GetPicSize(enSize[i], &stSize[i]);
          if (HI_SUCCESS != s32Ret)
          {
              SAMPLE_PRT("SAMPLE_COMM_SYS_GetPicSize failed!\n");
              return NULL;
          }
      }
  
      SAMPLE_COMM_VI_GetSensorInfo(&stViConfig);
      if(SAMPLE_SNS_TYPE_BUTT == stViConfig.astViInfo[0].stSnsInfo.enSnsType)
      {
          SAMPLE_PRT("Not set SENSOR%d_TYPE !\n",0);
          return NULL;
      }
  
      s32Ret = SAMPLE_VENC_CheckSensor(stViConfig.astViInfo[0].stSnsInfo.enSnsType,stSize[0]);
      if(s32Ret != HI_SUCCESS)
      {
          s32Ret = SAMPLE_VENC_ModifyResolution(stViConfig.astViInfo[0].stSnsInfo.enSnsType,&enSize[0],&stSize[0]);
          if(s32Ret != HI_SUCCESS)
          {
              return NULL;
          }
      }
  
      SAMPLE_VENC_GetDefaultVpssAttr(stViConfig.astViInfo[0].stSnsInfo.enSnsType, abChnEnable, stSize, &stParam);
  
      /******************************************
        step 1: init sys alloc common vb
      ******************************************/
      memset(&commVbAttr, 0, sizeof(commVbAttr));
      commVbAttr.supplementConfig = HI_FALSE;
      SAMPLE_VENC_GetCommVbAttr(stViConfig.astViInfo[0].stSnsInfo.enSnsType, &stParam, HI_FALSE, &commVbAttr);
  
      s32Ret = SAMPLE_VENC_SYS_Init(&commVbAttr);
      if(s32Ret != HI_SUCCESS)
      {
          SAMPLE_PRT("Init SYS err for %#x!\n", s32Ret);
          return NULL;
      }
  
      stViConfig.s32WorkingViNum       = 1;
      stViConfig.astViInfo[0].stDevInfo.ViDev     = ViDev;
      stViConfig.astViInfo[0].stPipeInfo.aPipe[0] = ViPipe;
      stViConfig.astViInfo[0].stChnInfo.ViChn     = ViChn;
      stViConfig.astViInfo[0].stChnInfo.enDynamicRange = DYNAMIC_RANGE_SDR8;
      stViConfig.astViInfo[0].stChnInfo.enPixFormat    = PIXEL_FORMAT_YVU_SEMIPLANAR_420;
  
      s32Ret = SAMPLE_VENC_VI_Init(&stViConfig, stParam.ViVpssMode);
      if(s32Ret != HI_SUCCESS)
      {
          SAMPLE_PRT("Init VI err for %#x!\n", s32Ret);
          return NULL;
      }
  
      s32Ret = SAMPLE_VENC_VPSS_Init(VpssGrp,&stParam);
      if (HI_SUCCESS != s32Ret)
      {
          SAMPLE_PRT("Init VPSS err for %#x!\n", s32Ret);
          goto EXIT_VI_STOP;
      }
  
      s32Ret = SAMPLE_COMM_VI_Bind_VPSS(ViPipe, ViChn, VpssGrp);
      if(s32Ret != HI_SUCCESS)
      {
          SAMPLE_PRT("VI Bind VPSS err for %#x!\n", s32Ret);
          goto EXIT_VPSS_STOP;
      }
  
     /******************************************
      start stream venc
      ******************************************/
  
      enRcMode = SAMPLE_VENC_GetRcMode();
  
      enGopMode = SAMPLE_VENC_GetGopMode();
      s32Ret = SAMPLE_COMM_VENC_GetGopAttr(enGopMode,&stGopAttr);
      if (HI_SUCCESS != s32Ret)
      {
          SAMPLE_PRT("Venc Get GopAttr for %#x!\n", s32Ret);
          goto EXIT_VI_VPSS_UNBIND;
      }
  
     /***encode h.265 **/
      s32Ret = SAMPLE_COMM_VENC_Start(VencChn[0], enPayLoad[0],enSize[0], enRcMode,u32Profile[0],bRcnRefShareBuf,&stGopAttr);
      if (HI_SUCCESS != s32Ret)
      {
          SAMPLE_PRT("Venc Start failed for %#x!\n", s32Ret);
          goto EXIT_VI_VPSS_UNBIND;
      }
  
      s32Ret = SAMPLE_COMM_VPSS_Bind_VENC(VpssGrp, VpssChn[0],VencChn[0]);
      if (HI_SUCCESS != s32Ret)
      {
          SAMPLE_PRT("Venc Get GopAttr failed for %#x!\n", s32Ret);
          goto EXIT_VENC_H265_STOP;
      }
#if 0
      /***encode h.264 **/
      s32Ret = SAMPLE_COMM_VENC_Start(VencChn[1], enPayLoad[1], enSize[1], enRcMode,u32Profile[1],bRcnRefShareBuf,&stGopAttr);
      if (HI_SUCCESS != s32Ret)
      {
          SAMPLE_PRT("Venc Start failed for %#x!\n", s32Ret);
          goto EXIT_VENC_H265_UnBind;
      }
  
      s32Ret = SAMPLE_COMM_VPSS_Bind_VENC(VpssGrp, VpssChn[1],VencChn[1]);
      if (HI_SUCCESS != s32Ret)
      {
          SAMPLE_PRT("Venc bind Vpss failed for %#x!\n", s32Ret);
          goto EXIT_VENC_H264_STOP;
      }
#endif
      /******************************************
       stream save process
      ******************************************/
//   pfnStreamPostProc pfnPostProc[VENC_MAX_CHN_NUM];
//   pfnPostProc[0] = PutVideoStreamToRingBuffer;
//   pfnPostProc[1] = NULL;
//   s32Ret = SAMPLE_COMM_VENC_StartGetStream_Ex(VencChn, s32ChnNum, pfnPostProc);
  //s32Ret = SAMPLE_COMM_VENC_StartGetStream(VencChn, s32ChnNum);

  
//   if (HI_SUCCESS != s32Ret)
//   {
//     SAMPLE_PRT("Start Venc failed!\n");
//     goto EXIT_VENC_H264_UnBind;
//   }

          SAMPLE_PRT("before vencsteamproc!!\n");
	pthread_create(&VencPid, 0, VENC_GetVencStreamProc, NULL);
//	VENC_GetVencStreamProc(NULL);
		//Create osd 
	//OSD_Handle_Init(1,1);
#if 0
	printf("whs:enter pq_import...\n");
	s32Ret = load_pq_bin();
    if(HI_SUCCESS != s32Ret) {
        printf("whs:import pq_bin failed...\n");
    } else {
        printf("whs:import pq_bin sucess...\n");
    }
#endif
    //	pthread_join(VencPid,0);
    	//pthread_detach(VencPid);

  printf("whs:test...\n");
       return NULL;
  printf("please press twice ENTER to exit this sample\n");
  getchar();
  getchar();

  /******************************************
  exit process
  ******************************************/
  //SAMPLE_COMM_VENC_StopGetStream_Ex();
  
EXIT_VENC_H264_UnBind:
    SAMPLE_COMM_VPSS_UnBind_VENC(VpssGrp,VpssChn[1],VencChn[1]);
EXIT_VENC_H264_STOP:
    SAMPLE_COMM_VENC_Stop(VencChn[1]);
EXIT_VENC_H265_UnBind:
    SAMPLE_COMM_VPSS_UnBind_VENC(VpssGrp,VpssChn[0],VencChn[0]);
EXIT_VENC_H265_STOP:
    SAMPLE_COMM_VENC_Stop(VencChn[0]);
EXIT_VI_VPSS_UNBIND:
    SAMPLE_COMM_VI_UnBind_VPSS(ViPipe,ViChn,VpssGrp);
EXIT_VPSS_STOP:
    SAMPLE_COMM_VPSS_Stop(VpssGrp,abChnEnable);
EXIT_VI_STOP:
    SAMPLE_COMM_VI_StopVi(&stViConfig);
    SAMPLE_COMM_SYS_Exit();
  
  return NULL;
#else
    HI_S32 i;
    HI_S32 s32Ret;
    SIZE_S          stSize[2];
    PIC_SIZE_E      enSize[2]     = {BIG_STREAM_SIZE,SMALL_STREAM_SIZE};
    HI_S32          s32ChnNum     = 2;
    VENC_CHN        VencChn[2]    = {0,1};
    HI_U32          u32Profile[2] = {0,0};
    PAYLOAD_TYPE_E  enPayLoad[2]  = {PT_H265,PT_h264};
    VENC_GOP_MODE_E enGopMode;
    VENC_GOP_ATTR_S stGopAttr;
    SAMPLE_RC_E     enRcMode;
    HI_BOOL         bRcnRefShareBuf = HI_TRUE;

    VI_DEV          ViDev        = 0;
    VI_PIPE         ViPipe       = 0;
    VI_CHN          ViChn        = 0;
    SAMPLE_VI_CONFIG_S stViConfig;

    VPSS_GRP        VpssGrp        = 0;
    VPSS_CHN        VpssChn[1]     = {0};
    HI_BOOL         abChnEnable[VPSS_MAX_PHY_CHN_NUM] = {HI_TRUE, HI_FALSE, HI_FALSE};
    SAMPLE_VPSS_CHN_ATTR_S stParam;
    SAMPLE_VB_ATTR_S commVbAttr;

    struct video_shm_sync_st *tmp_shm = (video_shm_sync_st*)arg;
    g_p_v_shm = (video_shm_sync_st*)arg;

    for(i=0; i<s32ChnNum; i++)
    {
        s32Ret = SAMPLE_COMM_SYS_GetPicSize(enSize[i], &stSize[i]);
        if (HI_SUCCESS != s32Ret)
        {
            SAMPLE_PRT("SAMPLE_COMM_SYS_GetPicSize failed!\n");
            return NULL;
        }
    }

    SAMPLE_COMM_VI_GetSensorInfo(&stViConfig);
    if(SAMPLE_SNS_TYPE_BUTT == stViConfig.astViInfo[0].stSnsInfo.enSnsType)
    {
        SAMPLE_PRT("Not set SENSOR%d_TYPE !\n",0);
        return NULL;
    }

    s32Ret = SAMPLE_VENC_CheckSensor(stViConfig.astViInfo[0].stSnsInfo.enSnsType,stSize[0]);
    if(s32Ret != HI_SUCCESS)
    {
        s32Ret = SAMPLE_VENC_ModifyResolution(stViConfig.astViInfo[0].stSnsInfo.enSnsType,&enSize[0],&stSize[0]);
        if(s32Ret != HI_SUCCESS)
        {
            return NULL;
        }
    }

    SAMPLE_VENC_GetDefaultVpssAttr(stViConfig.astViInfo[0].stSnsInfo.enSnsType, abChnEnable, stSize, &stParam);

    /******************************************
      step 1: init sys alloc common vb
    ******************************************/
    memset(&commVbAttr, 0, sizeof(commVbAttr));
    commVbAttr.supplementConfig = HI_FALSE;
    SAMPLE_VENC_GetCommVbAttr(stViConfig.astViInfo[0].stSnsInfo.enSnsType, &stParam, HI_FALSE, &commVbAttr);

    s32Ret = SAMPLE_VENC_SYS_Init(&commVbAttr);
    if(s32Ret != HI_SUCCESS)
    {
        SAMPLE_PRT("Init SYS err for %#x!\n", s32Ret);
        return NULL;
    }

    stViConfig.s32WorkingViNum       = 1;
    stViConfig.astViInfo[0].stDevInfo.ViDev     = ViDev;
    stViConfig.astViInfo[0].stPipeInfo.aPipe[0] = ViPipe;
    stViConfig.astViInfo[0].stChnInfo.ViChn     = ViChn;
    stViConfig.astViInfo[0].stChnInfo.enDynamicRange = DYNAMIC_RANGE_SDR8;
    stViConfig.astViInfo[0].stChnInfo.enPixFormat    = PIXEL_FORMAT_YVU_SEMIPLANAR_420;

    s32Ret = SAMPLE_VENC_VI_Init(&stViConfig, stParam.ViVpssMode);
    if(s32Ret != HI_SUCCESS)
    {
        SAMPLE_PRT("Init VI err for %#x!\n", s32Ret);
        return NULL;
    }

    s32Ret = SAMPLE_VENC_VPSS_Init(VpssGrp,&stParam);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("Init VPSS err for %#x!\n", s32Ret);
        goto EXIT_VI_STOP;
    }

    s32Ret = SAMPLE_COMM_VI_Bind_VPSS(ViPipe, ViChn, VpssGrp);
    if(s32Ret != HI_SUCCESS)
    {
        SAMPLE_PRT("VI Bind VPSS err for %#x!\n", s32Ret);
        goto EXIT_VPSS_STOP;
    }

   /******************************************
    start stream venc
    ******************************************/

    enRcMode = SAMPLE_VENC_GetRcMode();

    enGopMode = SAMPLE_VENC_GetGopMode();
    s32Ret = SAMPLE_COMM_VENC_GetGopAttr(enGopMode,&stGopAttr);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("Venc Get GopAttr for %#x!\n", s32Ret);
        goto EXIT_VI_VPSS_UNBIND;
    }

   /***encode h.265 **/
    s32Ret = SAMPLE_COMM_VENC_Start(VencChn[0], enPayLoad[0], enSize[0], enRcMode,u32Profile[0],bRcnRefShareBuf,&stGopAttr);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("Venc Start failed for %#x!\n", s32Ret);
        goto EXIT_VI_VPSS_UNBIND;
    }

    s32Ret = SAMPLE_COMM_VPSS_Bind_VENC(VpssGrp, VpssChn[0],VencChn[0]);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("Venc Get GopAttr failed for %#x!\n", s32Ret);
        goto EXIT_VENC_H265_STOP;
    }

    /******************************************
     stream save process
    ******************************************/
    pfnStreamPostProc pfnPostProc[VENC_MAX_CHN_NUM];
   // pfnPostProc[0] = PutVideoStreamToRingBuffer;

    s32Ret = SAMPLE_COMM_VENC_StartGetStream_Ex(VencChn, s32ChnNum, pfnPostProc);
   // s32Ret = SAMPLE_COMM_VENC_StartGetStream(VencChn, s32ChnNum);

    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("Start Venc failed!\n");
        goto EXIT_VENC_H265_UnBind;
    }

    return NULL;

    printf("please press twice ENTER to exit this sample\n");
    getchar();
    getchar();

    /******************************************
     exit process
    ******************************************/
    SAMPLE_COMM_VENC_StopGetStream_Ex();
#if 0
EXIT_VENC_H264_UnBind:
    SAMPLE_COMM_VPSS_UnBind_VENC(VpssGrp,VpssChn[1],VencChn[1]);
EXIT_VENC_H264_STOP:
    SAMPLE_COMM_VENC_Stop(VencChn[1]);
#endif
EXIT_VENC_H265_UnBind:
    SAMPLE_COMM_VPSS_UnBind_VENC(VpssGrp,VpssChn[0],VencChn[0]);
EXIT_VENC_H265_STOP:
    SAMPLE_COMM_VENC_Stop(VencChn[0]);
EXIT_VI_VPSS_UNBIND:
    SAMPLE_COMM_VI_UnBind_VPSS(ViPipe,ViChn,VpssGrp);
EXIT_VPSS_STOP:
    SAMPLE_COMM_VPSS_Stop(VpssGrp,abChnEnable);
EXIT_VI_STOP:
    SAMPLE_COMM_VI_StopVi(&stViConfig);
    SAMPLE_COMM_SYS_Exit();

    return NULL;
#endif
}

//HI_S32 PutVideoStreamToRingBuffer(VENC_STREAM_S *pstStream)
//{
//  HI_S32 i;
//  HI_S32 len = 0;
//  //printf("++PutVideoStreamToRingBuffer!\n");
//#if 0
//  if(-1 == g_vid_shm)
//  {
//    key_t key = ftok("shm_rtsp_v",'v');
//    g_vid_shm = shmget(key, VIDEO_BUF_NUM * sizeof(struct video_shm_sync_st), IPC_CREAT | 0666);//返回值为id�?
//
//    if(g_vid_shm < 0)
//    {
//      perror("video:shmget");
//      return HI_FAILURE;
//    }
//  }
//
//  if(g_p_v_shm == (void *)-1)
//  {
//    g_p_v_shm = (struct video_shm_sync_st *)shmat(g_vid_shm, NULL, 0);
//    if(g_p_v_shm == (void *)-1)
//    {
//      perror("shmat");
//      
//      printf("++PutAudioStreamToRingBuffer, mmap shm error!\n");
//      return HI_FAILURE;
//    }
//
//    struct video_shm_sync_st *g_pshm_tmp = g_p_v_shm;
//    for(i = 0; i < VIDEO_BUF_NUM; i++)
//    {
//      g_pshm_tmp->iFlag = 0xA5A5A5A5;
//      g_pshm_tmp++;
//    }
//    g_p_v_shm_tmp = g_p_v_shm;
//  }
//#endif
//
////   if (g_p_v_shm == (void *)-1)
////   {
////     printf("g_p_v_shm is null!\n");
////     return HI_FAILURE;
////   }
//#if 1
//  struct video_shm_sync_st *g_pshm_tmp = g_p_v_shm;
// // for(i = 0; i < VIDEO_BUF_NUM; i++)
// // {
// //   if(g_pshm_tmp->iFlag == 0xA5A5A5A5)
// //   {
// //     break;
// //   }
// //   else
// //   {
// //     g_pshm_tmp++;
// //   }
// // }
//
//  if(VIDEO_BUF_NUM == i)
//  {
//    //printf("++ no empty audio buffer!\n");
//    //return HI_FAILURE;
//    g_pshm_tmp = g_p_v_shm;
//  }
//#else
//    struct video_shm_sync_st *g_pshm_tmp = g_p_v_shm_tmp;
//    while(g_pshm_tmp->iFlag != 0xA5A5A5A5)
//    {
//        //return HI_SUCCESS;
//        sleep(0);
//    }
//    static HI_S32 s32Count = 0;
//    s32Count++;
//    if(s32Count == VIDEO_BUF_NUM)
//    {
//        g_p_v_shm_tmp = g_p_v_shm;
//        s32Count = 0;
//    }
//    else
//    {
//        g_p_v_shm_tmp++;
//    }
//#endif
//  for(i = 0; i < pstStream->u32PackCount; i++)
//  {
//    memcpy(g_pshm_tmp->pu8Addr + len, pstStream->pstPack[i].pu8Addr, pstStream->pstPack[i].u32Len);
//    len += pstStream->pstPack[i].u32Len;
//  }
//
//  g_pshm_tmp->iLen = len;
//  g_pshm_tmp->iFlag = 0xB4B4B4B4;
//
//  //if(shmdt(g_p_a_shm) < 0)
//  //{
//  //  perror("shmdt");
//  //}
//  //printf("--PutVideoStreamToRingBuffer (%d)!\n", len);
//
//  return HI_SUCCESS;
//}
