#include "stream-controller.h"
#include <string>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

using namespace std;


StreamController::StreamController(switch_core_session_t* session, Renderer *renderer, shared_ptr<Player> player)
       : _session(session), _video_pt(0), _video_rtp_sock(-1), _renderer(renderer), _player(player)
{
  memset(&_video_rtp_addr, 0, sizeof(_video_rtp_addr));
}

void StreamController::send_audio_packet(guint8 * buffer, gsize buffer_size)
{
  memcpy(_audio_frame.packet, buffer, buffer_size);
  _audio_frame.packetlen = buffer_size;

  switch_rtp_hdr_t *hdr = (switch_rtp_hdr_t *)_audio_frame.packet;
  uint32_t ts = ntohl(hdr->ts);
  if (_audio_pt) {
    hdr->pt = _audio_pt;
  }

  switch_byte_t *data = (switch_byte_t *) _audio_frame.packet;

  _audio_frame.m = (switch_bool_t)hdr->m;
  _audio_frame.timestamp = ts;
  _audio_frame.data = data + 12;
  _audio_frame.datalen = _audio_frame.packetlen - 12;

  switch_core_session_write_frame(_session, &_audio_frame, SWITCH_IO_FLAG_NONE, 0);
}

static int g_send_video_count = 0;

void StreamController::send_video_packet(guint8 * buffer, gsize buffer_size)
{
  // RTP-packetized H.264 from GStreamer rtph264pay
  memcpy(_video_frame.packet, buffer, buffer_size);
  _video_frame.packetlen = buffer_size;

  switch_rtp_hdr_t *hdr = (switch_rtp_hdr_t *)_video_frame.packet;
  uint32_t ts = ntohl(hdr->ts);
  if (_video_pt) {
    hdr->pt = _video_pt;
  }

  switch_byte_t *data = (switch_byte_t *) _video_frame.packet;

  _video_frame.m = (switch_bool_t)hdr->m;
  _video_frame.timestamp = ts;
  _video_frame.data = data + 12;
  _video_frame.datalen = _video_frame.packetlen - 12;
  switch_set_flag((&_video_frame), SFF_RAW_RTP);
  switch_set_flag((&_video_frame), SFF_PROXY_PACKET);

  // RTP-packetized H.264 from GStreamer rtph264pay
  memcpy(_video_frame.packet, buffer, buffer_size);
  _video_frame.packetlen = buffer_size;

  {
    switch_rtp_hdr_t *rtp_hdr = (switch_rtp_hdr_t *)_video_frame.packet;
    if (_video_pt) {
      rtp_hdr->pt = _video_pt;
    }
    _video_frame.m = (switch_bool_t)rtp_hdr->m;
    _video_frame.timestamp = ntohl(rtp_hdr->ts);
  }

  _video_frame.data = ((switch_byte_t *)_video_frame.packet) + 12;
  _video_frame.datalen = _video_frame.packetlen - 12;
  _video_frame.img = NULL;
  switch_set_flag((&_video_frame), SFF_RAW_RTP);
  switch_set_flag((&_video_frame), SFF_RAW_RTP_PARSE_FRAME);

  // Use write_encoded_video_frame - bypasses codec encode, writes directly to RTP
  switch_core_session_write_encoded_video_frame(_session, &_video_frame, SWITCH_IO_FLAG_FORCE, 0);

  g_send_video_count++;
  if (g_send_video_count <= 5 || g_send_video_count % 500 == 0) {
    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(_session), SWITCH_LOG_NOTICE, "send_video #%d: len=%d pt=%d\n",
      g_send_video_count, (int)buffer_size, hdr->pt);
  }

  g_send_video_count++;
  if (g_send_video_count <= 5 || g_send_video_count % 500 == 0) {
    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(_session), SWITCH_LOG_NOTICE, "send_video #%d: len=%d pt=%d\n",
      g_send_video_count, (int)buffer_size, hdr->pt);
  }
}

vector<char> StreamController::get_video_buffer() {
  Canvas canvas;
  _renderer->render_world(_player, canvas);
  int size = canvas.get_data_size();
  char* buffer = canvas.get_data();
  vector<char> result(buffer, buffer + size);
  return result;
}



void StreamController::start()
{

	switch_channel_t *channel = switch_core_session_get_channel(_session);

	// esix
  //	switch_frame_t write_frame = { 0 } /*, vid_frame = { 0 } */;
  int bytes;
	switch_codec_t codec = { 0 }, vid_codec = {0}, *read_vid_codec, *read_codec;
	unsigned char *aud_buffer;
	unsigned char *vid_buffer;
	uint32_t ts = 0, last = 0;
	switch_timer_t timer = { 0 };
	switch_payload_t pt = 0;
	switch_dtmf_t dtmf = { 0 };
	switch_frame_t *read_frame;
	//switch_codec_implementation_t read_impl = { 0 };
	switch_codec_implementation_t read_impl;
	memset(&read_impl, 0, sizeof(switch_codec_implementation_t));

  VideoCodec video_codec = INVALID_VIDEO_CODEC;

  switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(_session), SWITCH_LOG_NOTICE, "StreamController::start() CF_VIDEO=%d\n",
    switch_channel_test_flag(channel, CF_VIDEO) ? 1 : 0);

  if (!switch_channel_test_flag(channel, CF_VIDEO)) {
    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(_session), SWITCH_LOG_WARNING, "No video capability, returning\n");
    return;
  }


	//switch_channel_set_flag(channel, CF_VIDEO_PASSIVE);  // Don't set passive - it prevents outgoing video RTP
	switch_core_session_request_video_refresh(_session);
	switch_core_session_get_read_impl(_session, &read_impl);


  aud_buffer = (unsigned char *)switch_core_session_alloc(_session, SWITCH_RECOMMENDED_BUFFER_SIZE);
  vid_buffer = (unsigned char *)switch_core_session_alloc(_session, SWITCH_RECOMMENDED_BUFFER_SIZE);

  switch_channel_set_variable(channel, SWITCH_PLAYBACK_TERMINATOR_USED, "");

  //esix?
  //switch_channel_set_variable(channel, "rtp_force_video_fmtp", h.video_fmtp);
  switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(_session), SWITCH_LOG_NOTICE, "Answering channel\n");
  switch_channel_answer(channel);
  switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(_session), SWITCH_LOG_NOTICE, "Channel answered\n");

  if ((read_codec = switch_core_session_get_read_codec(_session))) {
    _audio_pt = read_codec->agreed_pt;
    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(_session), SWITCH_LOG_NOTICE, "Audio pt= %d (%s)\n", _audio_pt, read_codec->codec_interface->interface_name);
  } else {
    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(_session), SWITCH_LOG_WARNING, "No audio read codec!\n");
  }


  if ((read_vid_codec = switch_core_session_get_video_read_codec(_session))) {
  	pt = read_vid_codec->agreed_pt;
    _video_pt = read_vid_codec->agreed_pt;
    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(_session), SWITCH_LOG_NOTICE, "Video codec: %s, ianacode=%d, pt=%d\n",
      read_vid_codec->codec_interface->interface_name, read_vid_codec->implementation->ianacode, _video_pt);
    switch(read_vid_codec->implementation->ianacode) {
      case 115:
        video_codec = H263_1998;
        break;
      case 97:
        video_codec = H264;
        break;
      default:
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(_session), SWITCH_LOG_WARNING, "Unknown video ianacode = %d (%s)\n", read_vid_codec->implementation->ianacode, read_vid_codec->codec_interface->interface_name);
    }
  } else {
    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(_session), SWITCH_LOG_WARNING, "No video read codec!\n");
  }

  if(video_codec == INVALID_VIDEO_CODEC) {
    goto end;
  }


  _audio_frame.codec = &codec;
  _audio_frame.packet = aud_buffer;
  _audio_frame.data = aud_buffer + 12;
  _audio_frame.buflen = SWITCH_RECOMMENDED_BUFFER_SIZE - 12;
  switch_set_flag((&_audio_frame), SFF_RAW_RTP);
  switch_set_flag((&_audio_frame), SFF_PROXY_PACKET);

  _video_frame.codec = &vid_codec;
  _video_frame.packet = vid_buffer;
  _video_frame.data = vid_buffer + 12;
  _video_frame.buflen = SWITCH_RECOMMENDED_BUFFER_SIZE - 12;
  switch_set_flag((&_video_frame), SFF_RAW_RTP);
  switch_set_flag((&_video_frame), SFF_PROXY_PACKET);

	if (switch_core_timer_init(&timer, "soft", read_impl.microseconds_per_packet / 1000, read_impl.samples_per_packet, switch_core_session_get_pool(_session)) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(_session), SWITCH_LOG_ERROR, "Timer Activation Fail\n");
		switch_channel_set_variable(channel, SWITCH_CURRENT_APPLICATION_RESPONSE_VARIABLE, "Timer activation failed!");
		goto end;
	}
	switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(_session), SWITCH_LOG_NOTICE, "Timer activated: %dms, %d samples\n",
	  (int)(read_impl.microseconds_per_packet / 1000), (int)read_impl.samples_per_packet);

	if (switch_core_codec_init(&codec,
							   "PCMA",
							   NULL,
							   NULL,
                 8000,
                 20,
							   1, SWITCH_CODEC_FLAG_ENCODE | SWITCH_CODEC_FLAG_DECODE,
							   NULL, switch_core_session_get_pool(_session)) == SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(_session), SWITCH_LOG_DEBUG, "Audio Codec Activation Success\n");
	} else {
		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(_session), SWITCH_LOG_ERROR, "Audio Codec Activation Fail\n");
		switch_channel_set_variable(channel, SWITCH_CURRENT_APPLICATION_RESPONSE_VARIABLE, "Audio codec activation failed");
		goto end;
	}

	if (switch_core_codec_init(&vid_codec,
                 video_codec == H264 ? "H264" : "H263-1998",
							   NULL,
							   NULL,
							   0,
							   0,
							   1, SWITCH_CODEC_FLAG_ENCODE | SWITCH_CODEC_FLAG_DECODE,
							   NULL, switch_core_session_get_pool(_session)) == SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(_session), SWITCH_LOG_DEBUG, "Video Codec Activation Success: %s\n", vid_codec.implementation->iananame);
	} else {
		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(_session), SWITCH_LOG_ERROR, "Video Codec Activation Fail\n");
		switch_channel_set_variable(channel, SWITCH_CURRENT_APPLICATION_RESPONSE_VARIABLE, "Video codec activation failed");
		goto end;
	}
	switch_core_session_set_read_codec(_session, &codec);
	switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(_session), SWITCH_LOG_NOTICE, "Codecs activated: audio=PCMA video=%s\n",
	  video_codec == H264 ? "H264" : "H263-1998");

	switch_core_service_session_av(_session, SWITCH_FALSE, SWITCH_TRUE);
	switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(_session), SWITCH_LOG_NOTICE, "AV service started\n");

	// Set up direct UDP socket for video RTP (bypass FS RTP engine which doesn't transmit)
	{
	  const char *remote_video_ip = switch_channel_get_variable(channel, "switch_r_sdp_video_ip");
	  const char *remote_video_port = switch_channel_get_variable(channel, "switch_r_sdp_video_port");
	  // Fallback: try to get from remote SDP
	  if (!remote_video_ip) remote_video_ip = switch_channel_get_variable(channel, "remote_video_ip");
	  if (!remote_video_port) remote_video_port = switch_channel_get_variable(channel, "remote_video_port");

	  // Always log remote SDP for debugging
	  {
	    const char *r_sdp = switch_channel_get_variable(channel, SWITCH_R_SDP_VARIABLE);
	    const char *l_sdp = switch_channel_get_variable(channel, SWITCH_L_SDP_VARIABLE);
	    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(_session), SWITCH_LOG_NOTICE, "Remote SDP:\n%s\n", r_sdp ? r_sdp : "(null)");
	    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(_session), SWITCH_LOG_NOTICE, "Local SDP:\n%s\n", l_sdp ? l_sdp : "(null)");
	  }

	  // Get local video RTP port from FS's SDP answer
	  const char *local_video_port = NULL;
	  {
	    const char *l_sdp = switch_channel_get_variable(channel, "rtp_local_sdp_str");
	    if (l_sdp) {
	      const char *mvid = strstr(l_sdp, "m=video ");
	      if (mvid) {
	        local_video_port = mvid + 8; // points to port number
	      }
	    }
	  }

	  if (remote_video_ip && remote_video_port) {
	    _video_rtp_sock = socket(AF_INET, SOCK_DGRAM, 0);
	    _video_rtp_addr.sin_family = AF_INET;
	    _video_rtp_addr.sin_port = htons(atoi(remote_video_port));
	    const char *send_ip = remote_video_ip;
	    if (strcmp(remote_video_ip, "127.0.0.1") == 0) {
	      const char *gw = getenv("DOCKER_HOST_IP");
	      if (!gw) gw = "192.168.215.1";
	      send_ip = gw;
	    }
	    inet_aton(send_ip, &_video_rtp_addr.sin_addr);

	    // Bind to FS's local video RTP port so Linphone sees correct source
	    const char *local_sdp = switch_channel_get_variable(channel, "rtp_local_sdp_str");
	    if (local_sdp) {
	      const char *mvid = strstr(local_sdp, "m=video ");
	      if (mvid) {
	        int local_port = atoi(mvid + 8);
	        if (local_port > 0) {
	          struct sockaddr_in local_addr;
	          memset(&local_addr, 0, sizeof(local_addr));
	          local_addr.sin_family = AF_INET;
	          local_addr.sin_port = htons(local_port);
	          inet_aton(send_ip, &local_addr.sin_addr);
	          int reuse = 1;
	          setsockopt(_video_rtp_sock, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));
	          if (::bind(_video_rtp_sock, (struct sockaddr *)&local_addr, sizeof(local_addr)) == 0) {
	            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(_session), SWITCH_LOG_NOTICE, "Bound to local video port %d\n", local_port);
	          } else {
	            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(_session), SWITCH_LOG_WARNING, "Could not bind to port %d: %s\n", local_port, strerror(errno));
	          }
	        }
	      }
	    }

	    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(_session), SWITCH_LOG_NOTICE, "Direct video RTP to %s:%s (sock=%d)\n",
	      send_ip, remote_video_port, _video_rtp_sock);
	  } else {
	    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(_session), SWITCH_LOG_WARNING, "Could not determine remote video RTP address\n");
	  }
	}

  {
    //
    // run video + audio
    //
    //GstAudio gstAudio(this);  // Replaced with direct PCM audio generation
    GstVideo gstVideo(this, this, video_codec);

    // Audio state
    int audio_phase = 0;
    int fire_sound_timer = 0;    // counts down when firing
    int explode_sound_timer = 0; // counts down on explosion
    bool was_moving = false;
    unsigned int noise_seed = 12345;



  	while (switch_channel_ready(channel)) {
  		//switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(_session), SWITCH_LOG_DEBUG, "switch_channel_ready\n");



      switch_core_timer_next(&timer);

  		switch_core_session_read_frame(_session, &read_frame, SWITCH_IO_FLAG_NONE, 0);

      // Read and discard incoming video frames
      {
        switch_frame_t *vid_read_frame = NULL;
        switch_core_session_read_video_frame(_session, &vid_read_frame, SWITCH_IO_FLAG_NONE, 0);
      }

      // Generate game audio (160 samples per 20ms at 8kHz)
      {
        int16_t pcm_buf[160];
        memset(pcm_buf, 0, sizeof(pcm_buf));

        // Check game state for sounds
        bool is_moving = _player->get_bullet().is_active() || false; // TODO: check movement
        bool bullet_active = _player->get_bullet().is_active();

        // Detect new fire event
        if (bullet_active && fire_sound_timer == 0) {
          fire_sound_timer = 8; // 8 * 20ms = 160ms beep
        }

        // Check for explosions
        std::vector<explosion_t> explosions = _player->get_world()->get_explosions();
        if (!explosions.empty() && explode_sound_timer == 0) {
          explode_sound_timer = 15; // 300ms noise
        }

        for (int i = 0; i < 160; i++) {
          int sample = 0;

          // Fire sound: descending tone
          if (fire_sound_timer > 0) {
            int freq = 800 + fire_sound_timer * 100;
            sample += (int)(2000.0 * sin(audio_phase * freq * 2.0 * 3.14159 / 8000.0));
          }

          // Explosion: white noise
          if (explode_sound_timer > 0) {
            noise_seed = noise_seed * 1103515245 + 12345;
            sample += (int)(((noise_seed >> 16) & 0xFFF) - 2048);
          }

          // Comfort noise: very quiet low hum when nothing else playing
          if (fire_sound_timer == 0 && explode_sound_timer == 0) {
            sample += (int)(30.0 * sin(audio_phase * 120.0 * 2.0 * 3.14159 / 8000.0));
          }

          // Clamp
          if (sample > 32000) sample = 32000;
          if (sample < -32000) sample = -32000;
          pcm_buf[i] = (int16_t)sample;
          audio_phase++;
        }

        if (fire_sound_timer > 0) fire_sound_timer--;
        if (explode_sound_timer > 0) explode_sound_timer--;

        // Encode L16 -> A-law and write
        uint8_t alaw_buf[160];
        for (int i = 0; i < 160; i++) {
          // A-law encoding of 16-bit PCM
          int16_t s = pcm_buf[i];
          int sign = 0;
          if (s < 0) { s = -s; sign = 0x80; }
          int exp = 0, mantissa;
          if (s >= 256) {
            int shifted = s >> 4;
            for (exp = 1; exp < 7 && shifted > 31; exp++) shifted >>= 1;
            mantissa = (s >> (exp + 3)) & 0x0F;
          } else {
            mantissa = s >> 4;
          }
          alaw_buf[i] = (sign | (exp << 4) | mantissa) ^ 0xD5;
        }

        switch_frame_t audio_wr = { 0 };
        audio_wr.codec = &codec;
        audio_wr.data = alaw_buf;
        audio_wr.datalen = 160;
        audio_wr.samples = 160;
        switch_core_session_write_frame(_session, &audio_wr, SWITCH_IO_FLAG_NONE, 0);
      }

  		if (switch_channel_test_flag(channel, CF_BREAK)) {
  			switch_channel_clear_flag(channel, CF_BREAK);
  			break;
  		}

  		switch_ivr_parse_all_events(_session);

  		//check for dtmf interrupts
  		if (switch_channel_has_dtmf(channel)) {
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(_session), SWITCH_LOG_DEBUG, "Has dtmf\n");

  			const char * terminators = switch_channel_get_variable(channel, SWITCH_PLAYBACK_TERMINATORS_VARIABLE);
  			switch_channel_dequeue_dtmf(channel, &dtmf);

  			if (terminators && !strcasecmp(terminators, "none")) {
  				terminators = NULL;
  			}
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(_session), SWITCH_LOG_DEBUG, "Terminators=%s\n", terminators);
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(_session), SWITCH_LOG_DEBUG, "Dtmf digit: %c\n", dtmf.digit);

        switch(dtmf.digit) {

          case '6':  _player->command_move(E_EAST);   break;
          case '2':  _player->command_move(E_NORTH);  break;
          case '4':  _player->command_move(E_WEST);   break;
          case '8':  _player->command_move(E_SOUTH);  break;

          case '5':
            _player->command_fire();
            break;

          case '7':
            _player->get_world()->print();
            break;
        }

  			if (terminators && strchr(terminators, dtmf.digit)) {


					char sbuf[2] = {dtmf.digit, '\0'};
					switch_channel_set_variable(channel, SWITCH_PLAYBACK_TERMINATOR_USED, sbuf);
					break;
				}
  		}

  	} // while
  }

  switch_core_thread_session_end(_session);

  if (_video_rtp_sock >= 0) {
    close(_video_rtp_sock);
    _video_rtp_sock = -1;
  }

  switch_channel_set_variable(channel, SWITCH_CURRENT_APPLICATION_RESPONSE_VARIABLE, "OK");

  end:

  if (timer.interval) {
    switch_core_timer_destroy(&timer);
  }


  switch_core_session_set_read_codec(_session, NULL);


  if (switch_core_codec_ready(&codec)) {
  	switch_core_codec_destroy(&codec);
  }

  if (switch_core_codec_ready(&vid_codec)) {
  	switch_core_codec_destroy(&vid_codec);
  }

  done:
  switch_channel_clear_flag(channel, CF_VIDEO_PASSIVE);
}
