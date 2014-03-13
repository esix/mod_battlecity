#include "stream-controller.h"
#include <string>


using namespace std;


StreamController::StreamController(switch_core_session_t* session, Renderer *renderer, shared_ptr<Player> player)
       : _session(session), _video_pt(0), _renderer(renderer), _player(player)
{
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

void StreamController::send_video_packet(guint8 * buffer, gsize buffer_size)
{
  //switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(_session), SWITCH_LOG_DEBUG, "sending packet of len=%lu\n", buffer_size);

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

  switch_core_session_write_video_frame(_session, &_video_frame, SWITCH_IO_FLAG_NONE, 0);
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
	//switch_timer_t timer = { 0 };
	switch_payload_t pt = 0;
	switch_dtmf_t dtmf = { 0 };
	switch_frame_t *read_frame;
	//switch_codec_implementation_t read_impl = { 0 };
	switch_codec_implementation_t read_impl;
	memset(&read_impl, 0, sizeof(switch_codec_implementation_t));

  VideoCodec video_codec = INVALID_VIDEO_CODEC;

  //esix
  if (!switch_channel_test_flag(channel, CF_VIDEO)) {
    return;
  }


	switch_channel_set_flag(channel, CF_VIDEO_PASSIVE);
	switch_core_session_refresh_video(_session);
	switch_core_session_get_read_impl(_session, &read_impl);


  aud_buffer = (unsigned char *)switch_core_session_alloc(_session, SWITCH_RECOMMENDED_BUFFER_SIZE);
  vid_buffer = (unsigned char *)switch_core_session_alloc(_session, SWITCH_RECOMMENDED_BUFFER_SIZE);

  switch_channel_set_variable(channel, SWITCH_PLAYBACK_TERMINATOR_USED, "");

  //esix?
  //switch_channel_set_variable(channel, "rtp_force_video_fmtp", h.video_fmtp);
  switch_channel_answer(channel);

  if ((read_codec = switch_core_session_get_read_codec(_session))) {
    _audio_pt = read_codec->agreed_pt;
    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(_session), SWITCH_LOG_DEBUG, "Audio pt= %d (%s)\n", _audio_pt, read_codec->codec_interface->interface_name);
  }


  if ((read_vid_codec = switch_core_session_get_video_read_codec(_session))) {
  	pt = read_vid_codec->agreed_pt;
    _video_pt = read_vid_codec->agreed_pt;
    // esix
    switch(read_vid_codec->implementation->ianacode) {
      case 115:
        video_codec = H263_1998;
        break;
      case 97:
        video_codec = H264;
        break;
      default:
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(_session), SWITCH_LOG_DEBUG, "Unknown video codec = %d (%s)\n", read_vid_codec->implementation->ianacode, read_vid_codec->codec_interface->interface_name);
    }
    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(_session), SWITCH_LOG_DEBUG, "Read video codec = %s\n", read_vid_codec->codec_interface->interface_name);
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

	//if (switch_core_timer_init(&timer, "soft", read_impl.microseconds_per_packet / 1000,  read_impl.samples_per_packet, switch_core_session_get_pool(_session)) != SWITCH_STATUS_SUCCESS) {
  //	switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(_session), SWITCH_LOG_ERROR, "Timer Activation Fail\n");
  //	switch_channel_set_variable(channel, SWITCH_CURRENT_APPLICATION_RESPONSE_VARIABLE, "Timer activation failed!");
  //	goto end;
	//}

	if (switch_core_codec_init(&codec,
							   "PCMA",
							   NULL,
							   //h.audio_rate,
                 8000,
							   //h.audio_ptime,
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
							   //h.video_codec_name,
                 video_codec == H264 ? "H264" : "H263-1998",
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

	switch_core_service_session_av(_session, SWITCH_FALSE, SWITCH_TRUE);




  {
    //
    // run video + audio
    //
    GstAudio gstAudio(this);
    GstVideo gstVideo(this, this, video_codec);



  	while (switch_channel_ready(channel)) {
  		//switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(_session), SWITCH_LOG_DEBUG, "switch_channel_ready\n");



      //switch_cond_next();

  		//switch_core_timer_next(&timer);

  		switch_core_session_read_frame(_session, &read_frame, SWITCH_IO_FLAG_NONE, 0);

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

  switch_channel_set_variable(channel, SWITCH_CURRENT_APPLICATION_RESPONSE_VARIABLE, "OK");

  end:

  //if (timer.interval) {
  //  switch_core_timer_destroy(&timer);
  //}


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
