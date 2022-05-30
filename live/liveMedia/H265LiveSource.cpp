/**********
This library is free software; you can redistribute it and/or modify it under
the terms of the GNU Lesser General Public License as published by the
Free Software Foundation; either version 3 of the License, or (at your
option) any later version. (See <http://www.gnu.org/copyleft/lesser.html>.)

This library is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
more details.

You should have received a copy of the GNU Lesser General Public License
along with this library; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
**********/
// "liveMedia"
// Copyright (c) 1996-2021 Live Networks, Inc.  All rights reserved.
// A class for streaming data from a (static) memory buffer, as if it were a file.
// Implementation

#include "H265LiveSource.hh"
#include "GroupsockHelper.hh"

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
#include<stdio.h>
#include<stdlib.h>
#include<sys/ipc.h>
#include <sys/types.h>
#include<sys/shm.h>
#include<string.h>

////////// H265LiveSource //////////

H265LiveSource*
H265LiveSource::createNew(UsageEnvironment& env,
					Boolean deleteBufferOnClose,
					unsigned preferredFrameSize,
					unsigned playTimePerFrame) {
  return new H265LiveSource(env, deleteBufferOnClose, preferredFrameSize, playTimePerFrame);
}

H265LiveSource::H265LiveSource(UsageEnvironment& env,
							   Boolean deleteBufferOnClose,
							   unsigned preferredFrameSize,
							   unsigned playTimePerFrame)
  : FramedSource(env), fCurIndex(0), fDeleteBufferOnClose(deleteBufferOnClose),
    fPreferredFrameSize(preferredFrameSize), fPlayTimePerFrame(playTimePerFrame), fLastPlayTime(0),
    fLimitNumBytesToStream(False), fNumBytesToStream(0), fNumBufs(8), fCount(0), fVideoShm(-1), pVideoShmBuf((struct video_shm_sync_st *)-1) {
  unsigned i;

  if(-1 == fVideoShm)
  {
    key_t key = ftok("shm_rtsp_v",'v'); //键一定要唯一
    fVideoShm = shmget(key, fNumBufs * sizeof(struct video_shm_sync_st), IPC_CREAT | 0666);//返回值为id号

    if(fVideoShm < 0)
    {
      perror("shmget");
    }
  }

  if(pVideoShmBuf == (void *)-1)
  {
    pVideoShmBuf = (struct video_shm_sync_st *)shmat(fVideoShm, NULL, 0); //0表示共享内存可读可写
    if(pVideoShmBuf == (void *)-1)
    {
      perror("shmat");
    }
  }

  pVideoShmBufTmp = pVideoShmBuf;
  fCount = 0;
}

H265LiveSource::~H265LiveSource() {
  if (fDeleteBufferOnClose) delete[] fBuffer;
  if(shmdt((void *)pVideoShmBuf) < 0)
  {
    perror("shmdt");
  }

  pVideoShmBuf = (struct video_shm_sync_st *)-1;
}

void H265LiveSource::seekToByteAbsolute(u_int64_t byteNumber, u_int64_t numBytesToStream) {
  fCurIndex = byteNumber;
  if (fCurIndex > fBufferSize) fCurIndex = fBufferSize;

  fNumBytesToStream = numBytesToStream;
  fLimitNumBytesToStream = fNumBytesToStream > 0;
    printf("++seekToByteAbsolute 1\r\n");
}

void H265LiveSource::seekToByteRelative(int64_t offset, u_int64_t numBytesToStream) {
  int64_t newIndex = fCurIndex + offset;
  if (newIndex < 0) {
    fCurIndex = 0;
  } else {
    fCurIndex = (u_int64_t)offset;
    if (fCurIndex > fBufferSize) fCurIndex = fBufferSize;
  }

  fNumBytesToStream = numBytesToStream;
  fLimitNumBytesToStream = fNumBytesToStream > 0;
  printf("++seekToByteRelative 1\r\n");
}

void H265LiveSource::doGetNextFrame() {
#if 0
    // Try to read as many bytes as will fit in the buffer provided (or "fPreferredFrameSize" if less)
    if (fLimitNumBytesToStream && fNumBytesToStream < (u_int64_t)fMaxSize) {
      fMaxSize = (unsigned)fNumBytesToStream;
    }
    if (fPreferredFrameSize > 0 && fPreferredFrameSize < fMaxSize) {
      fMaxSize = fPreferredFrameSize;
    }
#ifdef READ_FROM_FILES_SYNCHRONOUSLY
    fFrameSize = fread(fTo, 1, fMaxSize, fFid);
#else
    if (fFidIsSeekable) {
      fFrameSize = fread(fTo, 1, fMaxSize, fFid);
    } else {
      // For non-seekable files (e.g., pipes), call "read()" rather than "fread()", to ensure that the read doesn't block:
      fFrameSize = read(fileno(fFid), fTo, fMaxSize);
    }
#endif
    if (fFrameSize == 0) {
      handleClosure();
      return;
    }
    fNumBytesToStream -= fFrameSize;
  
    // Set the 'presentation time':
    if (fPlayTimePerFrame > 0 && fPreferredFrameSize > 0) {
      if (fPresentationTime.tv_sec == 0 && fPresentationTime.tv_usec == 0) {
        // This is the first frame, so use the current time:
        gettimeofday(&fPresentationTime, NULL);
      } else {
        // Increment by the play time of the previous data:
        unsigned uSeconds = fPresentationTime.tv_usec + fLastPlayTime;
        fPresentationTime.tv_sec += uSeconds/1000000;
        fPresentationTime.tv_usec = uSeconds%1000000;
      }
  
      // Remember the play time of this data:
      fLastPlayTime = (fPlayTimePerFrame*fFrameSize)/fPreferredFrameSize;
      fDurationInMicroseconds = fLastPlayTime;
    } else {
      // We don't know a specific play time duration for this data,
      // so just record the current time as being the 'presentation time':
      gettimeofday(&fPresentationTime, NULL);
    }
  
    // Inform the reader that he has data:
#ifdef READ_FROM_FILES_SYNCHRONOUSLY
    // To avoid possible infinite recursion, we need to return to the event loop to do this:
    nextTask() = envir().taskScheduler().scheduleDelayedTask(0,
          (TaskFunc*)FramedSource::afterGetting, this);
#else
    // Because the file read was done from the event loop, we can call the
    // 'after getting' function directly, without risk of infinite recursion:
    FramedSource::afterGetting(this);
#endif

#else
  unsigned i;
  //printf("++doGetNextFrame 1\r\n");
#if 0
  struct video_shm_sync_st *pshm_tmp = pVideoShmBufTmp;
  while(pVideoShmBufTmp->iFlag != 0xB4B4B4B4)
  {
    sleep(0);
  }

  fCount++;
  if(fCount == fNumBufs)
  {
    pVideoShmBufTmp = pVideoShmBuf;
    fCount = 0;
  }
  else
  {
    pVideoShmBufTmp++;
  }
#else
  //先定位缓冲区
  struct video_shm_sync_st *pshm_tmp = pVideoShmBuf;
  for(i = 0; i < fNumBufs; i++)
  {
    if(pshm_tmp->iFlag == 0xB4B4B4B4)
    {
      break;
    }
    else
    {
      pshm_tmp++;
    }
  }

  if(fNumBufs == i)
  {
    //无可读缓冲，数据丢失
    //printf("++ no readable audio buffer! waiting!!!\n");
    pshm_tmp = pVideoShmBuf;
    while(pshm_tmp->iFlag != 0xB4B4B4B4)
    {
      sleep(0);
    }
  }
#endif
#if 0
  printf("audio buf(%X): %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X !\n", 
                    pshm_tmp->iLen,
                    pshm_tmp->pu8Addr[0], pshm_tmp->pu8Addr[1], 
                    pshm_tmp->pu8Addr[2], pshm_tmp->pu8Addr[3], 
                    pshm_tmp->pu8Addr[4], pshm_tmp->pu8Addr[5], 
                    pshm_tmp->pu8Addr[6], pshm_tmp->pu8Addr[7], 
                    pshm_tmp->pu8Addr[8], pshm_tmp->pu8Addr[9], 
                    pshm_tmp->pu8Addr[10], pshm_tmp->pu8Addr[11], 
                    pshm_tmp->pu8Addr[12], pshm_tmp->pu8Addr[13], 
                    pshm_tmp->pu8Addr[14], pshm_tmp->pu8Addr[15]);

#endif

  //TBD:需要设计退出机制

  fFrameSize = pshm_tmp->iLen;
  memcpy(fTo, pshm_tmp->pu8Addr, fFrameSize);
  pshm_tmp->iFlag = 0xA5A5A5A5;

  // Set the 'presentation time':
  if (fPlayTimePerFrame > 0 && fPreferredFrameSize > 0) {
    if (fPresentationTime.tv_sec == 0 && fPresentationTime.tv_usec == 0) {
      // This is the first frame, so use the current time:
      gettimeofday(&fPresentationTime, NULL);
    } else {
      // Increment by the play time of the previous data:
      unsigned uSeconds = fPresentationTime.tv_usec + fLastPlayTime;
      fPresentationTime.tv_sec += uSeconds/1000000;
      fPresentationTime.tv_usec = uSeconds%1000000;
    }

    // Remember the play time of this data:
    fLastPlayTime = (fPlayTimePerFrame*fFrameSize)/fPreferredFrameSize;
    fDurationInMicroseconds = fLastPlayTime;
  } else {
    // We don't know a specific play time duration for this data,
    // so just record the current time as being the 'presentation time':
    gettimeofday(&fPresentationTime, NULL);
  }

  // Inform the reader that he has data:
#ifdef READ_FROM_FILES_SYNCHRONOUSLY
  // To avoid possible infinite recursion, we need to return to the event loop to do this:
  nextTask() = envir().taskScheduler().scheduleDelayedTask(0,
        (TaskFunc*)FramedSource::afterGetting, this);
#else
  // Because the file read was done from the event loop, we can call the
  // 'after getting' function directly, without risk of infinite recursion:
  FramedSource::afterGetting(this);
#endif

#endif
}

//这里返回的数值是BANK_SIZE的最大值
unsigned int H265LiveSource::maxFrameSize() const
{
    return 300000;
}
