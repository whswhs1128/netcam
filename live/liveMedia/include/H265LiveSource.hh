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
// C++ header

#ifndef _H265_LIVE_SOURCE_HH
#define _H265_LIVE_SOURCE_HH

#ifndef _FRAMED_SOURCE_HH
#include "FramedSource.hh"
#endif

struct video_shm_sync_st
{  
    signed iFlag;//作为一个标志，0xB4B4B4B4：表示可读，0xA5A5A5A5表示可写
    signed iLen; //缓冲区数据长度
    char pu8Addr[0x100000];//记录写入和读取的文本
};

class H265LiveSource: public FramedSource {
public:
  static H265LiveSource* createNew(UsageEnvironment& env,
						 Boolean deleteBufferOnClose = True,
						 unsigned preferredFrameSize = 0,
						 unsigned playTimePerFrame = 0);

  u_int64_t bufferSize() const { return fBufferSize; }

  void seekToByteAbsolute(u_int64_t byteNumber, u_int64_t numBytesToStream = 0);
    // if "numBytesToStream" is >0, then we limit the stream to that number of bytes, before treating it as EOF
  void seekToByteRelative(int64_t offset, u_int64_t numBytesToStream = 0);

  unsigned int maxFrameSize() const;

protected:
  H265LiveSource(UsageEnvironment& env,
			       Boolean deleteBufferOnClose,
			       unsigned preferredFrameSize,
			       unsigned playTimePerFrame);
	// called only by createNew()

  virtual ~H265LiveSource();

private:
  // redefined virtual functions:
  virtual void doGetNextFrame();

private:
  u_int8_t* fBuffer;
  u_int64_t fBufferSize;
  u_int64_t fCurIndex;
  Boolean fDeleteBufferOnClose;
  unsigned fPreferredFrameSize;
  unsigned fPlayTimePerFrame;
  unsigned fLastPlayTime;
  Boolean fLimitNumBytesToStream;
  u_int64_t fNumBytesToStream; // used iff "fLimitNumBytesToStream" is True

  signed fVideoShm;
  unsigned fNumBufs;
  struct video_shm_sync_st *pVideoShmBuf;
  struct video_shm_sync_st *pVideoShmBufTmp;
  unsigned fCount;
};

#endif
