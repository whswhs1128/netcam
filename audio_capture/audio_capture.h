#ifndef __AUDIO_CAPTURE_H__
#define __AUDIO_CAPTURE_H__
#if 0
//共享内存方式
struct audio_shm_sync_st
{  
    int iFlag;//作为一个标志，0xB4B4B4B4：表示可读，0xA5A5A5A5表示可写
    int iLen; //缓冲区数据长度
    char pu8Addr[0x2000];//记录写入和读取的文本
};
#endif
//audio sharememory
#define AUDIO_BUF_NUM 2


void *thAudioCapture(void* parg);
#endif
