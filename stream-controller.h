#ifndef STREAM_CONTROLLER_H_
#define STREAM_CONTROLLER_H_

#include "gst-helper.h"
#include "world.h"
#include "renderer.h"

#include <switch.h>
#include <memory>


class StreamController : IVideoSender, IVideoBufferProvider, IAudioSender {
public:
  StreamController(switch_core_session_t* session, Renderer *renderer, std::shared_ptr<Player> player);
  void start();

  void send_audio_packet(guint8 * buffer, gsize buffer_size);
  void send_video_packet(guint8 * buffer, gsize buffer_size);

	std::vector<char> get_video_buffer();


private:
  StreamController();

  switch_core_session_t * _session;

  switch_frame_t          _audio_frame;
  switch_payload_t        _audio_pt;

  switch_frame_t          _video_frame;
	switch_payload_t        _video_pt;

	int                     _video_rtp_sock;
	struct sockaddr_in      _video_rtp_addr;

	Renderer              * _renderer;
	std::shared_ptr<Player> _player;
};



#endif // STREAM_CONTROLLER_H_
