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
// A 'ServerMediaSubsession' object that creates new, unicast, "RTPSink"s
// on demand, from a H265 Elementary Stream video file.
// C++ header

#ifndef _H265_LIVE_VIDEO_SERVER_MEDIA_SUBSESSION_HH
#define _H265_LIVE_VIDEO_SERVER_MEDIA_SUBSESSION_HH

#include "H265VideoFileServerMediaSubsession.hh"

class H265LiveVideoServerMediaSubsession: public H265VideoFileServerMediaSubsession  {
public:
  static H265LiveVideoServerMediaSubsession*
  createNew(UsageEnvironment& env, Boolean reuseFirstSource);

protected:
  H265LiveVideoServerMediaSubsession(UsageEnvironment& env, Boolean reuseFirstSource);
      // called only by createNew();
  virtual ~H265LiveVideoServerMediaSubsession();

  protected:
    //重定义虚函数
    FramedSource* createNewStreamSource(unsigned clientSessionId, unsigned& estBitrate);
};

#endif
