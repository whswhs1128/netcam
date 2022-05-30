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
// A source object for AAC audio files in ADTS format
// Implementation

#include "AACLiveSource.hh"
#include "InputFile.hh"
#include <GroupsockHelper.hh>

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

////////// AACLiveSource //////////

static unsigned const samplingFrequencyTable[16] = {
  96000, 88200, 64000, 48000,
  44100, 32000, 24000, 22050,
  16000, 12000, 11025, 8000,
  7350, 0, 0, 0
};

AACLiveSource*
AACLiveSource::createNew(UsageEnvironment& env) {

    return new AACLiveSource(env);
}

AACLiveSource
::AACLiveSource(UsageEnvironment& env)
  : FramedSource(env), fProfile(0xFF), fSamplingFrequency(48000), fNumChannels(2), fNumBufs(2), fAudioShm(-1), pAudioShmBuf((struct audio_shm_sync_st *)-1) {
  unsigned i;
  printf("++AACLiveSource::AACLiveSource()\r\n");

  if(-1 == fAudioShm)
  {
    key_t key = ftok("shm_rtsp_a",'a'); //键一定要唯一
    fAudioShm = shmget(key, fNumBufs * sizeof(struct audio_shm_sync_st), IPC_CREAT | 0666);//返回值为id号

    if(fAudioShm < 0)
    {
      perror("shmget");
    }
  }

  if(pAudioShmBuf == (void *)-1)
  {
    pAudioShmBuf = (struct audio_shm_sync_st *)shmat(fAudioShm, NULL, 0); //0表示共享内存可读可写
    if(pAudioShmBuf == (void *)-1)
    {
      perror("shmat");
    }
  }
  
  printf("--AACLiveSource::AACLiveSource()\r\n");
}

AACLiveSource::~AACLiveSource() {
printf("++AACLiveSource::~AACLiveSource()\r\n");
  if(shmdt((void *)pAudioShmBuf) < 0)
  {
    perror("shmdt");
  }

  pAudioShmBuf = (struct audio_shm_sync_st *)-1;
  printf("--AACLiveSource::~AACLiveSource()\r\n");

}

// Note: We should change the following to use asynchronous file reading, #####
// as we now do with ByteStreamFileSource. #####
void AACLiveSource::doGetNextFrame() {
  unsigned i;

  //先定位缓冲区
  struct audio_shm_sync_st *pshm_tmp = pAudioShmBuf;
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
    pshm_tmp = pAudioShmBuf;
    while(pshm_tmp->iFlag != 0xB4B4B4B4)
    {
      sleep(0);
    }
  }
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
  if(pshm_tmp->iLen < 7)
  {
    // The input source has ended:
    handleClosure();
    return;
  }

  if(fProfile == 0xFF) //还未收到任何帧
  {
    do {
      // Check the 'syncword':
      if(!(pshm_tmp->pu8Addr[0] == 0xFF && (pshm_tmp->pu8Addr[1]&0xF0) == 0xF0)) {
        //env.setResultMsg("Bad 'syncword' at start of ADTS file");
        break;
      }

      // Get and check the 'profile':
      u_int8_t profile = (pshm_tmp->pu8Addr[2]&0xC0)>>6; // 2 bits
      if(profile == 3) {
        //env.setResultMsg("Bad (reserved) 'profile': 3 in first frame of ADTS file");
        break;
      }

      // Get and check the 'sampling_frequency_index':
      u_int8_t sampling_frequency_index = (pshm_tmp->pu8Addr[2]&0x3C)>>2; // 4 bits
      if (samplingFrequencyTable[sampling_frequency_index] == 0) {
        //env.setResultMsg("Bad 'sampling_frequency_index' in first frame of ADTS file");
        break;
      }

      // Get and check the 'channel_configuration':
      u_int8_t channel_configuration
      = ((pshm_tmp->pu8Addr[2]&0x01)<<2)|((pshm_tmp->pu8Addr[3]&0xC0)>>6); // 3 bits

      // If we get here, the frame header was OK.
      printf("Read first frame: profile %d, "
      "sampling_frequency_index %d => samplingFrequency %d, "
      "channel_configuration %d\n",
      profile,
      sampling_frequency_index, samplingFrequencyTable[sampling_frequency_index],
      channel_configuration);

      fSamplingFrequency = samplingFrequencyTable[sampling_frequency_index];
      fNumChannels = channel_configuration == 0 ? 2 : channel_configuration;
      fuSecsPerFrame
        = (1024/*samples-per-frame*/*1000000) / fSamplingFrequency/*samples-per-second*/;
      fProfile = profile;
      // Construct the 'AudioSpecificConfig', and from it, the corresponding ASCII string:
      unsigned char audioSpecificConfig[2];
      u_int8_t const audioObjectType = profile + 1;
      audioSpecificConfig[0] = (audioObjectType<<3) | (sampling_frequency_index>>1);
      audioSpecificConfig[1] = (sampling_frequency_index<<7) | (channel_configuration<<3);
      sprintf(fConfigStr, "%02X%02X", audioSpecificConfig[0], audioSpecificConfig[1]);
    } while (0);
  }

  unsigned rawdata_off = 7;

  // Extract important fields from the headers:
  Boolean protection_absent = pshm_tmp->pu8Addr[1] & 0x01;
  u_int16_t frame_length
    = ((pshm_tmp->pu8Addr[3]&0x03)<<11) | (pshm_tmp->pu8Addr[4]<<3) | ((pshm_tmp->pu8Addr[5]&0xE0)>>5);
#ifdef DEBUG
  u_int16_t syncword = (pshm_tmp->pu8Addr[0]<<4) | (pshm_tmp->pu8Addr[1]>>4);
  fprintf(stderr, "Read frame: syncword 0x%x, protection_absent %d, frame_length %d\n", syncword, protection_absent, frame_length);
  if (syncword != 0xFFF) fprintf(stderr, "WARNING: Bad syncword!\n");
#endif
  unsigned numBytesToRead
    = frame_length > rawdata_off ? frame_length - rawdata_off : 0;

  // If there's a 'crc_check' field, skip it:
  if (!protection_absent) {
    rawdata_off += 2;
    numBytesToRead = numBytesToRead > 2 ? numBytesToRead - 2 : 0;
  }

  // Next, read the raw frame data into the buffer provided:
  if (numBytesToRead > fMaxSize) {
    fNumTruncatedBytes = numBytesToRead - fMaxSize;
    numBytesToRead = fMaxSize;
  }

  memcpy(fTo, pshm_tmp->pu8Addr + rawdata_off, numBytesToRead);
  pshm_tmp->iFlag = 0xA5A5A5A5;
  fFrameSize = numBytesToRead;

  //共享内存可能带有时间戳
  // Set the 'presentation time':
  if (fPresentationTime.tv_sec == 0 && fPresentationTime.tv_usec == 0) {
    // This is the first frame, so use the current time:
    gettimeofday(&fPresentationTime, NULL);
  } else {
    // Increment by the play time of the previous frame:
    unsigned uSeconds = fPresentationTime.tv_usec + fuSecsPerFrame;
    fPresentationTime.tv_sec += uSeconds/1000000;
    fPresentationTime.tv_usec = uSeconds%1000000;
  }

  fDurationInMicroseconds = fuSecsPerFrame;

  // Switch to another task, and inform the reader that he has data:
  nextTask() = envir().taskScheduler().scheduleDelayedTask(0,
				(TaskFunc*)FramedSource::afterGetting, this);
}
