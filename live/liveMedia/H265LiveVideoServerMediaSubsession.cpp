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
// on demand, from a H265 video file.
// Implementation

#include "H265LiveVideoServerMediaSubsession.hh"
#include "H265LiveFramedSource.hh"
#include "H265VideoStreamFramer.hh"

H265LiveVideoServerMediaSubsession*
H265LiveVideoServerMediaSubsession::createNew(UsageEnvironment& env,
					      Boolean reuseFirstSource) {
  return new H265LiveVideoServerMediaSubsession(env, reuseFirstSource);
}

H265LiveVideoServerMediaSubsession::H265LiveVideoServerMediaSubsession(UsageEnvironment& env,
								       Boolean reuseFirstSource)
  : H265VideoFileServerMediaSubsession(env, "stream_chn0.h265", reuseFirstSource) {
  
}

H265LiveVideoServerMediaSubsession::~H265LiveVideoServerMediaSubsession() {
    printf("++H265LiveVideoServerMediaSubsession::~H265LiveVideoServerMediaSubsession()");

}

FramedSource* H265LiveVideoServerMediaSubsession::createNewStreamSource(unsigned clientSessionId, unsigned& estBitrate) {
#if 1
  estBitrate = 500; // kbps, estimate

  // Create the video source:
  //H265LiveFramedSource* fileSource = H265LiveFramedSource::createNew(envir());
  
  H265LiveFramedSource* fileSource = H265LiveFramedSource::createNew(envir(), "stream_chn0.h265");
  if (fileSource == NULL) return NULL;
  fFileSize = fileSource->fileSize();

  // Create a framer for the Video Elementary Stream:

  return H265VideoStreamFramer::createNew(envir(), fileSource);
  #endif
  return H265VideoFileServerMediaSubsession::createNewStreamSource(clientSessionId, estBitrate);
}
