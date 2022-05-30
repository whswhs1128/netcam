#ifndef __VIDEO_CAPTURE_H__
#define __VIDEO_CAPTURE_H__
#if 0
//video sharememory
struct video_shm_sync_st
{  
    int iFlag;//0xB4B4B4B4£ºwritable£¬0xA5A5A5A5:readable
    int iLen; //the buffer size
    char pu8Addr[0x100000];//buffer
};
#endif
#define VIDEO_BUF_NUM 8 //30fps the buffer count is 8


void *thVideoCapture(void *);
#endif