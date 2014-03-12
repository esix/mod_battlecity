#ifndef GST_VIDEO_H_
#define GST_VIDEO_H_

#include <switch.h>
#include <gst/gst.h>
#include "renderer.h"
#include "world.h"
#include <vector>



enum VideoCodec {
  INVALID_VIDEO_CODEC,
  H263_1998,
  H264
};


class IAudioSender {
public:
  virtual void send_audio_packet(guint8 * buffer, gsize buffer_size) = 0;
};


class IVideoSender {
public:
  virtual void send_video_packet(guint8 * buffer, gsize buffer_size) = 0;
};


class IVideoBufferProvider {
public:
  virtual std::vector<char> get_video_buffer() = 0;
};


class GstAudio {
public:
  GstAudio(IAudioSender *sender);
  ~GstAudio();

private:
  GstAudio();

  static GstFlowReturn on_new_sample_from_sink (GstElement * elt, void * data);

  IAudioSender          * _sender;
  GstElement            * _pipeline;
};


class GstVideo {
public:
  GstVideo(IVideoSender *sender, IVideoBufferProvider *provider, VideoCodec codec);
  ~GstVideo();

private:
  GstVideo();

  static GstFlowReturn on_new_sample_from_sink (GstElement * elt, void * data);
  static gboolean      cb_need_data(GstElement *appsrc, guint unused_size,  gpointer user_data);

  IVideoSender          * _sender;
  IVideoBufferProvider  * _provider;
  GstElement            * _pipeline;
  VideoCodec              _codec;
};


#endif // GST_VIDEO_H_
