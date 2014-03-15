#include "gst-helper.h"
#include "renderer.h"
#include <stdio.h>

using namespace std;

static int g_video_frames_in = 0;
static int g_video_frames_out = 0;
static int g_audio_frames_out = 0;



// rtpmap:99 H264/90000
// fmtp:99 profile-level-id=42800D; packetization-mode=0; max-mbps=11880

#define GST_PIPELINE_START "appsrc name=src ! video/x-raw,format=RGB,bpp=24,depth=24,width=" SWIDTH ",height=" SHEIGHT ",pixel-aspect-ratio=1/1,framerate=15/1 ! videoconvert"
#define GST_PIPELINE_H264 "x264enc pass=cbr bitrate=1024 byte-stream=true bframes=0 key-int-max=5 tune=zerolatency speed-preset=ultrafast ! video/x-h264,stream-format=byte-stream,profile=baseline ! rtph264pay mtu=1200 config-interval=1"
#define GST_PIPELINE_H263_1998 "avenc_h263p ! video/x-h263"
#define GST_PIPELINE_END "appsink name=sink"



#include <cairo.h>



GstAudio::GstAudio(IAudioSender *sender) : _sender(sender)
{
  GstElement * sink;
  GstElement * src;
  GError *error = NULL;
  const gchar *descr = "audiotestsrc wave=4 ! alawenc ! rtppcmapay ! appsink name=sink";  // wave=4 = silence

  _pipeline = gst_parse_launch (descr, &error);

  if (error != NULL) {
    g_print ("could not construct audio pipeline: %s\n", error->message);
    g_error_free (error);
    return;
  }

  sink = gst_bin_get_by_name (GST_BIN (_pipeline), "sink");
  g_object_set (G_OBJECT (sink), "emit-signals", TRUE, "sync", FALSE, NULL);
  g_signal_connect (sink, "new-sample", G_CALLBACK (on_new_sample_from_sink), this);

  gst_element_set_state (_pipeline, GST_STATE_PAUSED);
  gst_element_set_state (_pipeline, GST_STATE_PLAYING);
  g_print ("gst_element_set_state (pipeline, GST_STATE_PLAYING);\n");
}

GstAudio::~GstAudio()
{
  if(_pipeline) {
    gst_element_set_state (_pipeline, GST_STATE_NULL);
    g_print ("gst_object_unref(pipeline)\n");
    gst_object_unref (_pipeline);
    _pipeline = NULL;
  }
}

GstFlowReturn GstAudio::on_new_sample_from_sink (GstElement * elt, void * data)
{
  GstAudio *gstAudio = (GstAudio *)data;

  GstFlowReturn ret = GST_FLOW_OK;

  GstSample *sample;
  g_signal_emit_by_name (elt, "pull-sample", &sample, NULL);

  if (sample) {
    GstBuffer *buffer;
    GstCaps *caps;
    GstStructure *s;
    gboolean res;
    gint width, height;
    caps = gst_sample_get_caps (sample);
    s = gst_caps_get_structure (caps, 0);
    buffer = gst_sample_get_buffer (sample);

    GstMapInfo map;
    gst_buffer_map (buffer, &map, GST_MAP_READ);
    gsize size = map.size;

    gstAudio->_sender->send_audio_packet(map.data, map.size);

    gst_buffer_unmap (buffer, &map);
    gst_sample_unref (sample);
  }else {
    g_print ("FAILED SAMPLE\n");
  }
  return ret;
}




GstVideo::GstVideo(IVideoSender *sender, IVideoBufferProvider *provider,  VideoCodec codec) : _sender(sender), _provider(provider), _codec(codec)
{
  GstElement * sink;
  GstElement * src;
  GError *error = NULL;
  const gchar *descr264;
  if(codec == H264) {
    descr264 = GST_PIPELINE_START " ! " GST_PIPELINE_H264 " ! " GST_PIPELINE_END;
  } else if(codec == H263_1998) {
    descr264 = GST_PIPELINE_START " ! " GST_PIPELINE_H263_1998 " ! " GST_PIPELINE_END;
  }

  _pipeline = gst_parse_launch (descr264, &error);

  if (error != NULL) {
    g_print ("could not construct pipeline: %s\n", error->message);
    g_error_free (error);
    return;
  }


  sink = gst_bin_get_by_name (GST_BIN (_pipeline), "sink");
  g_object_set (G_OBJECT (sink), "emit-signals", TRUE, "sync", FALSE, NULL);
  g_signal_connect (sink, "new-sample", G_CALLBACK (on_new_sample_from_sink), this);

  src = gst_bin_get_by_name (GST_BIN (_pipeline), "src");
  g_object_set (G_OBJECT (src),
	              "stream-type", 0,
	              "format", GST_FORMAT_TIME, NULL);
  g_signal_connect (src, "need-data", G_CALLBACK (cb_need_data), this);



  gst_element_set_state (_pipeline, GST_STATE_PAUSED);
  gst_element_set_state (_pipeline, GST_STATE_PLAYING);
  g_print ("gst_element_set_state (pipeline, GST_STATE_PLAYING);\n");
}


GstVideo::~GstVideo() {
  if(_pipeline) {
    gst_element_set_state (_pipeline, GST_STATE_NULL);
    g_print ("gst_object_unref(pipeline)\n");
    gst_object_unref (_pipeline);
    _pipeline = NULL;
  }
}


gboolean GstVideo::cb_need_data(GstElement *appsrc, guint unused_size, gpointer data) {
  //g_print ("cb_need_data unused_size==%d\n", (int)unused_size);

  GstVideo *gst_video = (GstVideo *)data;
  static GstClockTime timestamp = 0;
  GstBuffer *buffer;
  //GstMemory *mem;
  GstMapInfo info;
  GstFlowReturn ret;

  vector<char> video_data = gst_video->_provider->get_video_buffer();

  g_video_frames_in++;
  guint size = video_data.size();
  if (g_video_frames_in % 30 == 1) {
    fprintf(stderr, "VIDEO cb_need_data: frame=%d size=%d\n", g_video_frames_in, size);
  }

  buffer = gst_buffer_new_and_alloc (size);
  gst_buffer_map (buffer, &info, GST_MAP_WRITE);

  memcpy (info.data, &video_data[0], size);

  /*
  cairo_t *  cr;
  cairo_surface_t * imageSurface;
  cairo_format_t format = CAIRO_FORMAT_RGB24;
  int stride = cairo_format_stride_for_width (format, WIDTH);

  imageSurface = cairo_image_surface_create_for_data (info.data, format, WIDTH, HEIGHT, stride);
  cr = cairo_create(imageSurface);
  cairo_set_source_rgb(cr, 1.0, 0, 0);
  cairo_set_line_width (cr, 6.0);
  cairo_rectangle(cr, 10, 10, 200, 200);
  cairo_fill(cr);
  //cairo_surface_finish(imageSurface);
  */


  gst_buffer_unmap (buffer, &info);


  GST_BUFFER_PTS (buffer) = timestamp;
  GST_BUFFER_DURATION (buffer) = gst_util_uint64_scale_int (1, GST_SECOND, 2);

  timestamp += GST_BUFFER_DURATION (buffer);

  g_signal_emit_by_name (appsrc, "push-buffer", buffer, &ret);

  gst_buffer_unref (buffer);

  if (ret != GST_FLOW_OK) {
    /* something wrong, stop pushing */
    //g_main_loop_quit (loop);
    // OOPS...
    g_print ("cb_need_data ERROR\n");
    return FALSE;
  } else {
    return TRUE;
  }
}


GstFlowReturn GstVideo::on_new_sample_from_sink (GstElement * elt, void * data)
{
  GstVideo *gstVideo = (GstVideo *)data;

  GstFlowReturn ret = GST_FLOW_OK;


  GstSample *sample;
  //sample = gst_app_sink_pull_sample (GST_APP_SINK (elt));
  //gst_sample_unref (sample);

  g_signal_emit_by_name (elt, "pull-sample", &sample, NULL);

  if (sample) {
    GstBuffer *buffer;
    GstCaps *caps;
    GstStructure *s;
    gboolean res;
    gint width, height;
    caps = gst_sample_get_caps (sample);
    s = gst_caps_get_structure (caps, 0);
    buffer = gst_sample_get_buffer (sample);

    GstMapInfo map;
    gst_buffer_map (buffer, &map, GST_MAP_READ);
    gsize size = map.size;

    gstVideo->_sender->send_video_packet(map.data, map.size);

    g_video_frames_out++;
    if (g_video_frames_out % 30 == 1) {
      fprintf(stderr, "VIDEO on_new_sample: frame=%d rtp_size=%d\n", g_video_frames_out, (int)map.size);
    }

    gst_buffer_unmap (buffer, &map);
    gst_sample_unref (sample);


    /*res = gst_structure_get_int (s, "width", &width);
    res |= gst_structure_get_int (s, "height", &height);
    if (!res) {
      g_print ("could not get snapshot dimension\n");
      return -1;
    }*/
    //g_print ("on_new_sample_from_sink size=%d\n", (int)size);

  }else {
    g_print ("FAILED SAMPLE\n");
  }

  return ret;
}
