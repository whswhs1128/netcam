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

#include "audio_capture.h"
#include "video_capture.h"
//rtsp server
//#include "liveMedia.hh"
//
//#include "BasicUsageEnvironment.hh"
//#include "announceURL.hh"
#include<sys/ipc.h>
#include<sys/shm.h>
#include "sample_comm.h"
#include "rtsp_demo.h"
#include "comm.h"

//UsageEnvironment* env;

// To make the second and subsequent client for each stream reuse the same
// input stream as the first client (rather than playing the file from the
// start for each client), change the following "False" to "True":
//Boolean reuseFirstSource = False;

// To stream *only* MPEG-1 or 2 video "I" frames
// (e.g., to reduce network bandwidth),
// change the following "False" to "True":
//Boolean iFramesOnly = False;

//static void announceStream(RTSPServer* rtspServer, ServerMediaSession* sms,
//			   char const* streamName, char const* inputFileName) {
//  UsageEnvironment& env = rtspServer->envir();
//
//  env << "\n\"" << streamName << "\" stream, from the file \""
//      << inputFileName << "\"\n";
//  announceURL(rtspServer, sms);
//}

int g_avid_shm = -1;
void *g_p_av_shm = (void *)-1;//
////struct video_shm_sync_st *g_p_v_shm_tmp = (video_shm_sync_st*)-1;//


extern "C" int rtsp_start() {
#if 0
    key_t key = ftok("shm_rtsp_av",'c');//
    g_avid_shm = shmget(key, VIDEO_BUF_NUM * sizeof(struct video_shm_sync_st) + AUDIO_BUF_NUM * sizeof(struct audio_shm_sync_st), IPC_CREAT | 0666);//返回值为id�?
  
    if(g_avid_shm < 0)
    {
      perror("ipc_rtsp:shmget");
    }
  
    g_p_av_shm = (void *)shmat(g_avid_shm, NULL, 0);//
    if(g_p_av_shm == (void *)-1)
    {
      perror("ipc_rtsp:shmat");
    }
  
    //
    struct video_shm_sync_st *g_pvshm = (struct video_shm_sync_st *)g_p_av_shm;
    struct video_shm_sync_st *g_pvshm_tmp = g_pvshm;
  
    for(int i = 0; i < VIDEO_BUF_NUM; i++)
    {
      g_pvshm_tmp->iFlag = 0xA5A5A5A5;
      g_pvshm_tmp++;
    }
    
    struct audio_shm_sync_st *g_pashm = (struct audio_shm_sync_st *)g_pvshm_tmp;//(audio_shm_sync_st *)(g_p_av_shm + VIDEO_BUF_NUM * sizeof(struct video_shm_sync_st));
    struct audio_shm_sync_st *g_pashm_tmp = g_pashm;
  
    for(int i = 0; i < AUDIO_BUF_NUM; i++)
    {
      g_pashm_tmp->iFlag = 0xA5A5A5A5;
      g_pashm_tmp++;
    }
  
    printf("++ vbuf(%i) abuf(%i)!\r\n", (unsigned int)g_pvshm, (unsigned int)g_pashm);
#endif

#if 0
  // Begin by setting up our usage environment:
  TaskScheduler* scheduler = BasicTaskScheduler::createNew();
  env = BasicUsageEnvironment::createNew(*scheduler);

  UserAuthenticationDatabase* authDB = NULL;
#ifdef ACCESS_CONTROL
  // To implement client access control to the RTSP server, do the following:
  authDB = new UserAuthenticationDatabase;
  authDB->addUserRecord("username1", "password1"); // replace these with real strings
  // Repeat the above with each <username>, <password> that you wish to allow
  // access to the server.
#endif

  // Create the RTSP server:
  RTSPServer* rtspServer = RTSPServer::createNew(*env, 8554, authDB);
  if (rtspServer == NULL) {
    *env << "Failed to create RTSP server: " << env->getResultMsg() << "\n";
    exit(1);
  }

  char const* descriptionString
    = "Session streamed by \"RTSPAvServer\"";


  char const* streamName = "h265_ac3_rtsp";
  char const* inputFileName = "rtsp.av";
  // NOTE: This *must* be a Program Stream; not an Elementary Stream
  ServerMediaSession* sms
    = ServerMediaSession::createNew(*env, streamName, streamName,
    		      descriptionString);

  //sms->addSubsession(AACLiveMediaSubsession::createNew(*env, reuseFirstSource));
  //sms->addSubsession(H265LiveVideoServerMediaSubsession::createNew(*env, reuseFirstSource));
  //sms->addSubsession(H265VideoFileServerMediaSubsession::createNew(*env, "stream_chn0.h265", reuseFirstSource));
  
  sms->addSubsession(H265LiveMediaSubsession::createNew(*env, reuseFirstSource));
 // sms->addSubsession(AACLiveMediaSubsession::createNew(*env, reuseFirstSource));

  rtspServer->addServerMediaSession(sms);

  announceStream(rtspServer, sms, streamName, inputFileName);

  // Also, attempt to create a HTTP server for RTSP-over-HTTP tunneling.
  // Try first with the default HTTP port (80), and then with the alternative HTTP
  // port numbers (8000 and 8080).

  if (rtspServer->setUpTunnelingOverHTTP(80) || rtspServer->setUpTunnelingOverHTTP(8000) || rtspServer->setUpTunnelingOverHTTP(8080)) {
    *env << "\n(We use port " << rtspServer->httpServerPortNum() << " for optional RTSP-over-HTTP tunneling.)\n";
  } else {
    *env << "\n(RTSP-over-HTTP tunneling is not available.)\n";
  }
#endif

  pthread_t thAudioID = 0;
  pthread_t thVideoID = 0;
  struct video_shm_sync_st *g_pvshm = (struct video_shm_sync_st *)g_p_av_shm;
  /**/
  // pthread_create(&thAudioID, NULL, thAudioCapture, g_pashm);
  //thAudioCapture(NULL);
  //sleep(1);


 //pthread_create(&thVideoID, NULL, thVideoCapture, g_pvshm);
  thVideoCapture(g_pvshm);
#if 0
  env->taskScheduler().doEventLoop(); // does not return
#endif
  // while(1);
  return 0; // only to prevent compiler warning
}
