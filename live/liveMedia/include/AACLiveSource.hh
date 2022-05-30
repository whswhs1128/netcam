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
// C++ header

#ifndef _AAC_LIVE_SOURCE_HH
#define _AAC_LIVE_SOURCE_HH

#ifndef _FRAMED_FILE_SOURCE_HH
#include "FramedFileSource.hh"
#endif

struct audio_shm_sync_st
{  
    signed iFlag;//作为一个标志，0xB4B4B4B4：表示可读，0xA5A5A5A5表示可写
    signed iLen; //缓冲区数据长度
    char pu8Addr[0x400];//记录写入和读取的文本
};

class AACLiveSource: public FramedSource {
public:
  static AACLiveSource* createNew(UsageEnvironment& env/*,
				       unsigned preferredFrameSize, unsigned playTimePerFrame*/);

  unsigned samplingFrequency() const { return fSamplingFrequency; }
  unsigned numChannels() const { return fNumChannels; }
  char const* configStr() const { return fConfigStr; }
      // returns the 'AudioSpecificConfig' for this stream (in ASCII form)

private:
  AACLiveSource(UsageEnvironment& env);
	// called only by createNew()

  virtual ~AACLiveSource();

private:
  // redefined virtual functions:
  virtual void doGetNextFrame();

private:
  u_int8_t fProfile;
  unsigned fSamplingFrequency;
  unsigned fNumChannels;
  unsigned fuSecsPerFrame;
  char fConfigStr[5];

  signed fAudioShm;
  unsigned fNumBufs;
  struct audio_shm_sync_st *pAudioShmBuf;
};

#endif
