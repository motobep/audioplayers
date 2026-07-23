#include "audio_player.h"
#include <flutter_linux/flutter_linux.h>
#include <stdarg.h>
#include "logger.h"

extern "C" {
#include <gst/gst.h>
}

#define STR_LINK_TROUBLESHOOTING \
  "https://github.com/bluefireteam/audioplayers/blob/main/troubleshooting.md"

#define TIMOUT_CLOCK_TIME (100 * 1000000)  // 100 ms
#define APP_REFRESH_TIME (250)             // ms


inline float getInBounds(float value, float min, float max) {
  if (value > max) {
    return max;
  } else if (value < min) {
    return min;
  }
  return value;
}

static void on_pad_added_from_src(GstElement* src,
                                  GstPad* pad,
                                  gpointer udata) {
  logger::warn("on_pad_added_from_src(src: %p, pad: %p, udata: %p)", src, pad,
               udata);

  // Checking
  GstCaps* caps = gst_pad_get_current_caps(pad);
  if (caps == nullptr) {
    logger::error("gst_pad_get_current_caps is null");
  }
  GstStructure* s = gst_caps_get_structure(caps, 0);
  if (s == nullptr) {
    logger::error("gst_caps_get_structure is null");
  }
  const gchar* name = gst_structure_get_name(s);

  bool has_prefix = g_str_has_prefix(name, "audio/");
  gst_caps_unref(caps);
  if (!has_prefix) {
    logger::error("No caps prefix");
    return;
  }
  // Checking End

  GstElement* audioconvert = (GstElement*)udata;
  GstPad* sinkpad = gst_element_get_static_pad(audioconvert, "sink");
  if (gst_pad_is_linked(sinkpad)) {
    logger::warn("Pad already linked");
    gst_object_unref(sinkpad);
    return;
  }

  if (gst_pad_link(pad, sinkpad) != GST_PAD_LINK_OK) {
    logger::error("Failed to link pad to audioconvert\n");
  }
  gst_object_unref(sinkpad);
}

void on_source_setup(GstElement* bin, GstElement* source, GstElement* udata) {
  // logger::log("on_source_setup()");
  AudioPlayer* player = (AudioPlayer*)udata;
  GParamSpec* has_prop =
      g_object_class_find_property(G_OBJECT_GET_CLASS(source), "proxy");
  if (has_prop != 0) {
    // logger::log("has prop");
    gchararray proxy;
    // g_object_get(G_OBJECT(source), "proxy", &proxy, NULL);
    // logger::log("proxy before: '%s'", proxy);
    if (!player->http_proxy.empty()) {
      g_object_set(G_OBJECT(source), "proxy", player->http_proxy.c_str(), NULL);
    } else {
      logger::log("http_proxy is empty");
    }

    g_object_get(G_OBJECT(source), "proxy", &proxy, NULL);
    logger::log("proxy after: '%s'", proxy);
  } else {
    // logger::log("no proxy");
  }
};

static void on_need_data(GstElement* appsrc, guint length, gpointer udata) {
  logger::log("need-data (length=%u)", length);
}

static void on_enough_data(GstElement* appsrc, gpointer udata) {
  logger::log("enough-data");
}

AudioPlayer::AudioPlayer(std::string playerId,
                         FlMethodChannel* methodChannel,
                         FlEventChannel* eventChannel)
    : _playerId(playerId), _eventChannel(eventChannel) {
  logger::log("AudioPlayer()");
  // GStreamer lib only needs to be initialized once, but doing it while
  // registering the plugin can be problematic as it likely needs a GUI to be
  // present. Calling it multiple times is fine.
  gst_init(NULL, NULL);

  setbuf(stdout, NULL);

  pipeline = gst_pipeline_new("pipeline");

  // Bytes
  appsrc = gst_element_factory_make("appsrc", "appsrc");
  app_decodebin = gst_element_factory_make("decodebin", "app_decodebin");
  // OR Uri
  uridecodebin = gst_element_factory_make("uridecodebin", "uridecodebin");

  audioconvert = gst_element_factory_make("audioconvert", "audioconvert");
  audioresample = gst_element_factory_make("audioresample", "audioresample");
  volume_elem = gst_element_factory_make("volume", "volume_elem");
  equalizer = gst_element_factory_make("equalizer-nbands", "equalizer");
  panorama = gst_element_factory_make("audiopanorama", NULL);
  audiosink = gst_element_factory_make("autoaudiosink", NULL);

  // Setup equalizer and stereo balance controller
  if (!pipeline || !appsrc || !app_decodebin || !uridecodebin ||
      !audioconvert || !audioresample || !volume_elem || !equalizer ||
      !panorama || !audiosink) {
    perror("Failed to create elements\n");
    throw "Failed to create elements\n";
  }

  gst_bin_add_many(GST_BIN(pipeline), uridecodebin, audioconvert, audioresample,
                   volume_elem, equalizer, panorama, audiosink, NULL);

  if (!gst_element_link_many(audioconvert, audioresample, volume_elem,
                             equalizer, panorama, audiosink, NULL)) {
    perror("Can't link elements\n");
    throw "Can't link elements\n";
  }

  g_object_set(appsrc,                      //
               "is-live", true,             //
               "format", GST_FORMAT_BYTES,  //
               "block", false,              //
               NULL);

  // g_object_set(G_OBJECT(uridecodebin), "use-buffering", true, NULL);
  // g_object_set(G_OBJECT(uridecodebin), "download", true, NULL);

  /* Connect pad-added handlers for both decodebins to link to audioconvert */
  g_signal_connect(app_decodebin, "pad-added",
                   G_CALLBACK(on_pad_added_from_src), this->audioconvert);
  g_signal_connect(uridecodebin, "pad-added", G_CALLBACK(on_pad_added_from_src),
                   this->audioconvert);

  g_signal_connect(uridecodebin, "source-setup", G_CALLBACK(on_source_setup),
                   this);

  g_signal_connect(appsrc, "enough-data", G_CALLBACK(on_enough_data), NULL);
  g_signal_connect(appsrc, "need-data", G_CALLBACK(on_need_data), NULL);

  // Set panorama and equalizer
  g_object_set(G_OBJECT(panorama), "method", 1, NULL);
  g_object_set(G_OBJECT(equalizer), "num-bands", AudioPlayer::eqNumBands, NULL);

  // Set bands
  for (int i = 0; i < AudioPlayer::eqNumBands; i++) {
    eqBands[i] =
        gst_child_proxy_get_child_by_index(GST_CHILD_PROXY(equalizer), i);

    // Set initial values
    SetGain(i, 0.0);
    SetBandwidth(i, 0.0);
    SetFrequency(i, 20.0);
  }

  // Set limits
  const double gainLimits[] = {-24.0, 12.0};
  const double bandwidthLimits[] = {0.0, 20000.0};
  const double frequencyLimits[] = {20.0, 20000.0};
  fl_value_set_string(_limitsMap, "gain",
                      fl_value_new_float_list(gainLimits, 2));
  fl_value_set_string(_limitsMap, "bandwidth",
                      fl_value_new_float_list(bandwidthLimits, 2));
  fl_value_set_string(_limitsMap, "frequency",
                      fl_value_new_float_list(frequencyLimits, 2));

  bus = gst_element_get_bus(pipeline);

  // Watch bus messages for one time events
  gst_bus_add_watch(bus, (GstBusFunc)AudioPlayer::OnBusMessage, this);

  // Refresh continuously to emit reoccurring events
  _refreshId = g_timeout_add(APP_REFRESH_TIME,
                             (GSourceFunc)AudioPlayer::OnRefresh, this);
}

AudioPlayer::~AudioPlayer() {}

void AudioPlayer::SetSourceUrl(std::string url) {
  logger::log("SetSourceSourceUrl");

  SrcState srcState = GetSrcState();
  if (srcState == SRC_STATE_APP) {
    // Stop pipeline
    SetPipelineState(GST_STATE_NULL);

    // Unset urldecodebin
    logger::log("Unset appsrc");
    // gst_element_unlink(appsrc, app_decodebin);
    // gst_element_unlink(app_decodebin, audioconvert);
    gst_bin_remove(GST_BIN(pipeline), appsrc);
    // Ref once more. Just because.
    gst_object_ref(app_decodebin);
    if (!gst_bin_remove(GST_BIN(pipeline), app_decodebin)) {
      logger::log("Can't remove app_decodebin");
      // throw "Can't remove app_decodebin\n";
    }

    gst_bin_add(GST_BIN(pipeline), uridecodebin);
  }

  if (_url != url) {
    _url = url;
    SetPipelineState(GST_STATE_NULL);
    _isPlaying = false;  // TODO: should you do that?
    if (!_url.empty()) {
      g_object_set(GST_OBJECT(uridecodebin), "uri", _url.c_str(), NULL);
      if (pipeline->current_state != GST_STATE_READY) {
        GstStateChangeReturn ret = SetPipelineState(GST_STATE_READY);
        if (ret == GST_STATE_CHANGE_FAILURE) {
          throw "Unable to set the pipeline to GST_STATE_READY.";
        }
      }
    }
  }
  _isSourceInitialized = true;
  this->OnPrepared(true);

  logger::log("Switched to url: %s", url.c_str());
}

void AudioPlayer::SetSourceByteStream() {
  logger::log("SetSourceByteStream");

  SrcState srcState = GetSrcState();
  if (srcState == SRC_STATE_URI) {
    // Stop pipeline
    SetPipelineState(GST_STATE_NULL);

    // Unset urldecodebin
    logger::log("Unset uriSrc");
    // gst_element_unlink(uridecodebin, audioconvert);
    gst_bin_remove(GST_BIN(pipeline), uridecodebin);

    logger::log("Is null");
    if (app_decodebin == NULL) {
      logger::log("app_decodebin is NULL");
    }
    logger::log("Is element");
    if (!GST_IS_ELEMENT(app_decodebin)) {
      logger::log("app_decodebin is not GstElement");
    }

    logger::log("Adding appsrc");
    gst_bin_add(GST_BIN(pipeline), appsrc);
    logger::log("Adding app_decodebin");
    if (!gst_bin_add(GST_BIN(pipeline), app_decodebin)) {
      logger::log("Can't add app_decodebin");
      // throw "Can't add app_decodebin\n";
    }

    if (!gst_element_link(appsrc, app_decodebin)) {
      perror("bad linking\n");
      throw "Can't link appsrc\n";
    }

    if (pipeline->current_state != GST_STATE_READY) {
      GstStateChangeReturn ret = SetPipelineState(GST_STATE_READY);
      if (ret == GST_STATE_CHANGE_FAILURE) {
        throw "Unable to set the pipeline to GST_STATE_READY.";
      }
    }
    logger::log("Switched to appsrc");
  }
  _isSourceInitialized = true;
  this->OnPrepared(true);
}

int64_t AudioPlayer::PushBuffer(const guint8* buffer, ssize_t len) {
  logger::log("PushBuffer(%ld)", len);
  printPipelineState("PushBuffer");

  if (len > 0) {
    GstBuffer* gstbuf = gst_buffer_new_allocate(NULL, (gsize)len, NULL);
    gst_buffer_fill(gstbuf, 0, buffer, (gsize)len);

    GstFlowReturn ret;
    g_signal_emit_by_name(appsrc, "push-buffer", gstbuf, &ret);
    gst_buffer_unref(gstbuf);

    if (ret != GST_FLOW_OK) {
      g_printerr("Push error: %d\n", ret);
      return 0;
    }
    return 1;
  }

  if (len == 0) {
    /* EOF on stdin — signal EOS */
    logger::log("Buffer end-of-stream emit");
    g_signal_emit_by_name(appsrc, "end-of-stream", NULL);
  } else if (len < 0) {
    g_printerr("Read error: %s\n", strerror(errno));
  }
  return 0;
}

void AudioPlayer::FlushBuffers(bool isHard) {
  guint64 current_level_buffers;
  g_object_get(G_OBJECT(appsrc), "current-level-buffers",
               &current_level_buffers, NULL);
  guint64 current_level_bytes;
  g_object_get(G_OBJECT(appsrc), "current-level-bytes", &current_level_bytes,
               NULL);

  logger::warn("buffers: %lu, bytes: %lu", current_level_buffers,
               current_level_bytes);

  guint64 in;
  g_object_get(G_OBJECT(appsrc), "in", &in, NULL);
  guint64 out;
  g_object_get(G_OBJECT(appsrc), "out", &out, NULL);
  logger::warn("in: %lu, out: %lu", in, out);

  if (isHard) {
    flushBuffersHard();
    return;
  }
  flushBuffersSoft(true);
}

void AudioPlayer::flushBuffersHard() {
  SetPipelineState(GST_STATE_READY);
  // printPipelineState("flushing: after ready");
  if (_isPlaying) {
    SetPipelineState(GST_STATE_PLAYING);
  } else {
    SetPipelineState(GST_STATE_PAUSED);
  }
  printPipelineState("flushing: end");
}

void AudioPlayer::flushBuffersSoft(bool is_segment) {
  logger::log("Flushing with events");

  // Flushing pipeline
  GstEvent* flush_start_event = gst_event_new_flush_start();
  gst_element_send_event(pipeline, flush_start_event);

  GstEvent* flush_stop_event = gst_event_new_flush_stop(TRUE);
  gst_element_send_event(pipeline, flush_stop_event);

  if (is_segment) {
    logger::log("is_segment");

    GstSegment segment;
    gst_segment_init(&segment, GST_FORMAT_TIME);
    segment.start = 0;  // Start from 0 for new init.m4a
    segment.position = 0;
    segment.time = 0;
    segment.stop = GST_CLOCK_TIME_NONE;

    GstPad* appsrc_pad = gst_element_get_static_pad(appsrc, "src");
    gst_pad_push_event(appsrc_pad, gst_event_new_segment(&segment));

    // Flushing pipeline again
    GstEvent* flush_start_event = gst_event_new_flush_start();
    gst_element_send_event(pipeline, flush_start_event);

    GstEvent* flush_stop_event = gst_event_new_flush_stop(TRUE);
    gst_element_send_event(pipeline, flush_stop_event);
  }

  logger::log("Flushed");
}

void AudioPlayer::ReleaseMediaSource() {
  if (_isPlaying)
    _isPlaying = false;
  if (_isSourceInitialized)
    _isSourceInitialized = false;
  _url.clear();

  GstState pipelineState;
  // logger::log("ReleaseMediaSource: gst_element_get_state");
  GstStateChangeReturn ret =
      gst_element_get_state(pipeline, &pipelineState, NULL, TIMOUT_CLOCK_TIME);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    logger::log("ReleaseMediaSource failed");
  } else {
    // logger::log("ReleaseMediaSource:\tout");
    if (ret != GST_STATE_CHANGE_SUCCESS) {
      logger::log("ReleaseMediaSource not SUCCESS (%u)", ret);
    }
    if (pipelineState > GST_STATE_NULL) {
      SetPipelineState(GST_STATE_NULL);
    }
  }
}

gboolean AudioPlayer::OnBusMessage(GstBus* bus,
                                   GstMessage* message,
                                   AudioPlayer* data) {
  // logger::log("OnBusMessage (%d)", GST_MESSAGE_TYPE(message));
  switch (GST_MESSAGE_TYPE(message)) {
    case GST_MESSAGE_ERROR: {
      // Just info
      GError* err;
      gchar* debug;

      gst_message_parse_error(message, &err, &debug);
      data->OnMediaError(err, debug);
      g_error_free(err);
      g_free(debug);
      break;
    }
    case GST_MESSAGE_STATE_CHANGED:
      GstState old_state, new_state;

      gst_message_parse_state_changed(message, &old_state, &new_state, NULL);
      data->OnMediaStateChange(GST_MESSAGE_SRC(message), &old_state,
                               &new_state);
      break;
    case GST_MESSAGE_EOS:
      // Just info
      logger::log("GST_MESSAGE_EOS");
      data->OnPlaybackEnded();
      break;
    case GST_MESSAGE_DURATION_CHANGED:
      // Just info
      data->OnDurationUpdate();
      break;
    case GST_MESSAGE_ASYNC_DONE:
      // Just info
      if (!data->_isSeekCompleted) {
        data->OnSeekCompleted();
        data->_isSeekCompleted = true;
      }
      break;
    default:
      // For more GstMessage types see:
      // https://gstreamer.freedesktop.org/documentation/gstreamer/gstmessage.html?gi-language=c#enumerations
      break;
  }

  // Continue watching for messages
  return TRUE;
};

// Compare with refresh_ui in
// https://gstreamer.freedesktop.org/documentation/tutorials/basic/toolkit-integration.html?gi-language=c#walkthrough
gboolean AudioPlayer::OnRefresh(AudioPlayer* data) {
  if (data->pipeline == nullptr) {
    return FALSE;
  }
  // We do not want to update anything unless we are in PLAYING state
  GstState pipelineState;
  // logger::log("OnRefresh: gst_element_get_state");
  GstStateChangeReturn ret = gst_element_get_state(
      data->pipeline, &pipelineState, NULL, TIMOUT_CLOCK_TIME);
  if (ret != GST_STATE_CHANGE_SUCCESS) {
    // logger::log("OnRefresh not SUCCESS (%u)", ret);
  }
  if (ret == GST_STATE_CHANGE_FAILURE) {
    logger::log("OnRefresh failed");
  } else {
    // logger::log("OnRefresh:\tout");
    if (pipelineState == GST_STATE_PLAYING) {
      data->OnPositionUpdate();
    }
  }
  return TRUE;
}

void AudioPlayer::OnMediaError(GError* error, gchar* debug) {
  if (this->_eventChannel) {
    gchar const* code = "LinuxAudioError";
    gchar const* message;
    auto detailsStr = std::string(error->message) + " (Domain: " +
                      std::string(g_quark_to_string(error->domain)) +
                      ", Code: " + std::to_string(error->code) + ")";
    FlValue* details = fl_value_new_string(detailsStr.c_str());
    // https://gstreamer.freedesktop.org/documentation/gstreamer/gsterror.html#enumerations
    if (error->domain == GST_STREAM_ERROR) {
      message =
          "Failed to set source. For troubleshooting, "
          "see: " STR_LINK_TROUBLESHOOTING;
    } else {
      message = "Unknown GstGError. See details.";
    }
    this->OnError(code, message, details, &error);
  }
}

void AudioPlayer::OnError(const gchar* code,
                          const gchar* message,
                          FlValue* details,
                          GError** error) {
  if (this->_eventChannel) {
    fl_event_channel_send_error(this->_eventChannel, code, message, details,
                                nullptr, error);
  } else {
    std::ostringstream oss;
    oss << "Error: " << code << "; message=" << message;
    g_print("%s\n", oss.str().c_str());
  }
}

void AudioPlayer::OnMediaStateChange(GstObject* src,
                                     GstState* old_state,
                                     GstState* new_state) {
  // logger::log("OnMediaStateChange (%d -> %d)", *old_state, *new_state);
  if (!pipeline) {
    this->OnError("LinuxAudioError",
                  "Player was already disposed (OnMediaStateChange).", nullptr,
                  nullptr);
    return;
  }

  if (src == GST_OBJECT(pipeline)) {
    if (*new_state == GST_STATE_READY) {
    } else if (*old_state == GST_STATE_PAUSED &&
               *new_state == GST_STATE_PLAYING) {
      // Just info
      OnPositionUpdate();
      OnDurationUpdate();
    }
  }
}

void AudioPlayer::OnPrepared(bool isPrepared) {
  logger::log("isPrepared: %u", isPrepared);
  if (this->_eventChannel) {
    g_autoptr(FlValue) map = fl_value_new_map();
    fl_value_set_string(map, "event", fl_value_new_string("audio.onPrepared"));
    fl_value_set_string(map, "value", fl_value_new_bool(isPrepared));
    fl_event_channel_send(this->_eventChannel, map, nullptr, nullptr);
  }
}

void AudioPlayer::OnPositionUpdate() {
  if (this->_eventChannel) {
    g_autoptr(FlValue) map = fl_value_new_map();
    fl_value_set_string(map, "event",
                        fl_value_new_string("audio.onCurrentPosition"));
    fl_value_set_string(map, "value",
                        fl_value_new_int(GetPosition().value_or(0)));
    fl_event_channel_send(this->_eventChannel, map, nullptr, nullptr);
  }
}

void AudioPlayer::OnDurationUpdate() {
  logger::log("OnDurationUpdate");
  if (this->_eventChannel) {
    g_autoptr(FlValue) map = fl_value_new_map();
    fl_value_set_string(map, "event", fl_value_new_string("audio.onDuration"));
    fl_value_set_string(map, "value",
                        fl_value_new_int(GetDuration().value_or(0)));
    fl_event_channel_send(this->_eventChannel, map, nullptr, nullptr);
  }
}

void AudioPlayer::OnSeekCompleted() {
  if (this->_eventChannel) {
    OnPositionUpdate();
    g_autoptr(FlValue) map = fl_value_new_map();
    fl_value_set_string(map, "event",
                        fl_value_new_string("audio.onSeekComplete"));
    fl_value_set_string(map, "value", fl_value_new_bool(true));
    fl_event_channel_send(this->_eventChannel, map, nullptr, nullptr);
  }
}

void AudioPlayer::OnPlaybackEnded() {
  if (this->_eventChannel) {
    g_autoptr(FlValue) map = fl_value_new_map();
    fl_value_set_string(map, "event", fl_value_new_string("audio.onComplete"));
    fl_value_set_string(map, "value", fl_value_new_bool(true));
    fl_event_channel_send(this->_eventChannel, map, nullptr, nullptr);
  }
}

void AudioPlayer::OnLog(const gchar* message) {
  if (this->_eventChannel) {
    g_autoptr(FlValue) map = fl_value_new_map();
    fl_value_set_string(map, "event", fl_value_new_string("audio.onLog"));
    fl_value_set_string(map, "value", fl_value_new_string(message));

    fl_event_channel_send(this->_eventChannel, map, nullptr, nullptr);
  }
}

bool AudioPlayer::GetEnabled() {
  return _isEnabled;
}

// how to dynamicly change element in pipiline
// https://gstreamer.freedesktop.org/documentation/application-development/advanced/pipeline-manipulation.html?gi-language=c#changing-elements-in-a-pipeline
void AudioPlayer::SetEnabled(bool isEnabled) {
  if (!isEnabled) {
    if (_isEnabled) {
      // Disable
      for (int i = 0; i < AudioPlayer::eqNumBands; i++) {
        // Saving previous gains
        gdouble gain;
        g_object_get(AudioPlayer::eqBands[i], "gain", &gain, NULL);
        eqWhenDisabledGains[i] = gain;
        // Disable current gains
        SetGain(i, 0.0);
      }
      _isEnabled = isEnabled;
    }
  } else {
    if (!_isEnabled) {
      // Enable
      _isEnabled = isEnabled;
      for (int i = 0; i < AudioPlayer::eqNumBands; i++) {
        // Use saved gains
        float gain = eqWhenDisabledGains[i];
        SetGain(i, gain);
      }
    }
  }
}

int AudioPlayer::GetNumberOfBands() {
  return __AUDIO_PLAYER_NUM_BUNDS;
}

FlValue* AudioPlayer::GetLimits() {
  return _limitsMap;
}

FlValue* AudioPlayer::GetBand(int bandIndex) {
  gdouble gain;
  gdouble bandwidth;
  gdouble freq;

  if (_isEnabled) {
    g_object_get(AudioPlayer::eqBands[bandIndex], "gain", &gain, NULL);
  } else {
    gain = eqWhenDisabledGains[bandIndex];
  }

  g_object_get(AudioPlayer::eqBands[bandIndex], "bandwidth", &bandwidth, NULL);
  g_object_get(AudioPlayer::eqBands[bandIndex], "freq", &freq, NULL);

  fl_value_set_string(_bandMap, "gain", fl_value_new_float(gain));
  fl_value_set_string(_bandMap, "bandwidth", fl_value_new_float(bandwidth));
  fl_value_set_string(_bandMap, "frequency", fl_value_new_float(freq));

  return _bandMap;
}

void AudioPlayer::SetBand(int bandIndex, FlValue* band) {
  if (!equalizer) {
    this->OnLog("Equalizer was not initialized");
    return;
  }

  auto flGain = fl_value_lookup_string(band, "gain");
  if (flGain != nullptr) {
    double gain = fl_value_get_float(flGain);
    SetGain(bandIndex, gain);
  }

  auto flBandwidth = fl_value_lookup_string(band, "bandwidth");
  if (flBandwidth != nullptr) {
    double value = fl_value_get_float(flBandwidth);
    SetBandwidth(bandIndex, value);
  }

  auto flFrequency = fl_value_lookup_string(band, "frequency");
  if (flFrequency != nullptr) {
    double value = fl_value_get_float(flFrequency);
    SetFrequency(bandIndex, value);
  }
}

void AudioPlayer::SetGain(int bandIndex, float value) {
  value = getInBounds(value, -24.0, 12.0);
  if (_isEnabled) {
    g_object_set(AudioPlayer::eqBands[bandIndex], "gain", value, NULL);
  } else {
    eqWhenDisabledGains[bandIndex] = value;
  }
}

void AudioPlayer::SetBandwidth(int bandIndex, float value) {
  value = getInBounds(value, 0.0, 20000.0);
  g_object_set(AudioPlayer::eqBands[bandIndex], "bandwidth", value, NULL);
}

void AudioPlayer::SetFrequency(int bandIndex, float value) {
  value = getInBounds(value, 20.0, 20000.0);
  g_object_set(AudioPlayer::eqBands[bandIndex], "freq", value, NULL);
}

void AudioPlayer::SetBalance(float balance) {
  if (balance > 1.0f) {
    balance = 1.0f;
  } else if (balance < -1.0f) {
    balance = -1.0f;
  }
  g_object_set(G_OBJECT(panorama), "panorama", balance, NULL);
}

void AudioPlayer::SetLooping(bool isLooping) {
  _isLooping = isLooping;
}

bool AudioPlayer::GetLooping() {
  return _isLooping;
}

void AudioPlayer::SetVolume(double volume) {
  if (volume > 1) {
    volume = 1;
  } else if (volume < 0) {
    volume = 0;
  }
  g_object_set(G_OBJECT(volume_elem), "volume", volume, NULL);
}

/**
 * A rate of 1.0 means normal playback rate, 2.0 means double speed.
 * Negatives values means backwards playback.
 * A value of 0.0 will pause the player.
 *
 * @param position the position in milliseconds
 * @param rate the playback rate (speed)
 */
void AudioPlayer::SetPlayback(int64_t position, double rate) {
  // logger::log("SetPlayback");
  if (rate != 0 && _playbackRate != rate) {
    _playbackRate = rate;
  }

  // See:
  // https://gstreamer.freedesktop.org/documentation/tutorials/basic/playback-speed.html?gi-language=c
  if (!_isSeekCompleted) {
    return;
  }
  // logger::log("seek completed");
  if (rate == 0) {
    // Do not set rate if it's 0, rather pause.
    Pause();
    return;
  }

  _isSeekCompleted = false;

  GstEvent* seek_event;
  if (rate > 0) {
    seek_event = gst_event_new_seek(
        rate, GST_FORMAT_TIME,
        GstSeekFlags(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE),
        GST_SEEK_TYPE_SET, position * GST_MSECOND, GST_SEEK_TYPE_NONE, -1);
  } else {
    seek_event = gst_event_new_seek(
        rate, GST_FORMAT_TIME,
        GstSeekFlags(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE),
        GST_SEEK_TYPE_SET, 0, GST_SEEK_TYPE_SET, position * GST_MSECOND);
  }

  if (!gst_element_send_event(pipeline, seek_event)) {
    logger::log("SetPlayback NO boy");
    int prevPos = GetPosition().value_or(-1);
    this->OnLog((std::string("Could not set playback to position ") +
                 std::to_string(position) + std::string(" and rate ") +
                 std::to_string(rate) + std::string(" prevPos=(") +
                 std::to_string(prevPos) + std::string(")."))
                    .c_str());
    _isSeekCompleted = true;
  } else {
    logger::log("Set to position: %ld", position);
  }
}

void AudioPlayer::SetPlaybackRate(double rate) {
  SetPlayback(GetPosition().value_or(0), rate);
}

/**
 * @param position the position in milliseconds
 */
void AudioPlayer::SetPosition(int64_t position) {
  SetPlayback(position, _playbackRate);
}

/**
 * @return int64_t the position in milliseconds
 */
std::optional<int64_t> AudioPlayer::GetPosition() {
  gint64 current = 0;
  if (!gst_element_query_position(pipeline, GST_FORMAT_TIME, &current)) {
    this->OnLog("Could not query current position.");
    return std::nullopt;
  }
  return std::make_optional(current / 1000000);
}

/**
 * @return int64_t the duration in milliseconds
 */
std::optional<int64_t> AudioPlayer::GetDuration() {
  gint64 duration = 0;
  if (!gst_element_query_duration(pipeline, GST_FORMAT_TIME, &duration)) {
    // FIXME: Get duration for MP3 with variable bit rate with gst-discoverer:
    // https://gstreamer.freedesktop.org/documentation/pbutils/gstdiscoverer.html?gi-language=c#gst_discoverer_info_get_duration
    this->OnLog("Could not query current duration.");
    return std::nullopt;
  }
  return std::make_optional(duration / 1000000);
}

void AudioPlayer::Play() {
  SetPosition(0);
  Resume();
}

void AudioPlayer::Pause() {
  if (!_isSourceInitialized) {
    logger::log("Pause(): _isSourceInitialized = false");
    return;
  }
  if (_isPlaying) {
    _isPlaying = false;
  }
  logger::log("pausing literulrryyrlry");
  // NOTICE: pause only audiosink to avoid flushing pipeline
  GstStateChangeReturn ret = gst_element_set_state(audiosink, GST_STATE_PAUSED);
  // GstStateChangeReturn ret = SetPipelineState(GST_STATE_PAUSED);
  if (ret == GST_STATE_CHANGE_SUCCESS) {
    OnPositionUpdate();  // Update to exact position when pausing
  } else if (ret == GST_STATE_CHANGE_FAILURE) {
    throw "Unable to set the pipeline to GST_STATE_PAUSED.";
  }
}

void AudioPlayer::Resume() {
  logger::log("Resume");
  if (!_isSourceInitialized) {
    logger::log("Resume(): _isSourceInitialized = false");
    return;
  }
  if (!_isPlaying) {
    _isPlaying = true;
  }
  GstStateChangeReturn ret_set = SetPipelineState(GST_STATE_PLAYING);
  printPipelineState("Resume pipeline state ");

  if (ret_set == GST_STATE_CHANGE_SUCCESS) {
    // Update position and duration when start playing, as no event is emitted
    // elsewhere
    logger::log("Resume SUCCESS");
    OnPositionUpdate();
    OnDurationUpdate();
  } else if (ret_set == GST_STATE_CHANGE_FAILURE) {
    throw "Unable to set the pipeline to GST_STATE_PLAYING.";
  } else {
    logger::log("Resume else: %d", ret_set);
  }
}

inline void bin_remove_and_null(GstBin* bin, GstElement** el_p) {
  gst_bin_remove(bin, *el_p);
  *el_p = nullptr;
}

void AudioPlayer::Dispose() {
  if (!pipeline)
    throw "Player was already disposed (Dispose)";
  ReleaseMediaSource();

  g_source_remove(_refreshId);

  if (bus) {
    gst_bus_remove_watch(bus);
    gst_object_unref(GST_OBJECT(bus));
    bus = nullptr;
  }

  bin_remove_and_null(GST_BIN(pipeline), &appsrc);
  bin_remove_and_null(GST_BIN(pipeline), &app_decodebin);
  bin_remove_and_null(GST_BIN(pipeline), &uridecodebin);
  bin_remove_and_null(GST_BIN(pipeline), &audioconvert);
  bin_remove_and_null(GST_BIN(pipeline), &audioresample);
  bin_remove_and_null(GST_BIN(pipeline), &volume_elem);
  bin_remove_and_null(GST_BIN(pipeline), &equalizer);
  bin_remove_and_null(GST_BIN(pipeline), &panorama);
  bin_remove_and_null(GST_BIN(pipeline), &audiosink);

  gst_object_unref(GST_OBJECT(pipeline));
  // Do not dispose method channel as it is used by multiple players!
  g_clear_object(&_eventChannel);
  _eventChannel = nullptr;
  pipeline = nullptr;
}

GstStateChangeReturn AudioPlayer::SetPipelineState(GstState state) {
  return gst_element_set_state(pipeline, state);
}

void AudioPlayer::printPipelineState(const char* s) {
  GstState pipelineState;
  GstStateChangeReturn ret =
      gst_element_get_state(pipeline, &pipelineState, NULL, TIMOUT_CLOCK_TIME);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    logger::warn("printPipelineState() get pipeline state failed");
    return;
  }

  if (s == NULL) {
    s = "";
  }
  logger::warn("[%s] Pipeline state: %d", s, pipelineState);
}

SrcState AudioPlayer::GetSrcState() {
  GstElement* uriSrc = gst_bin_get_by_name(GST_BIN(pipeline), "uridecodebin");
  GstElement* appSrc = gst_bin_get_by_name(GST_BIN(pipeline), "appsrc");
  if (uriSrc != NULL && appSrc == NULL) {
    return SRC_STATE_URI;
  }
  if (uriSrc == NULL && appSrc != NULL) {
    return SRC_STATE_APP;
  }
  perror("Bad pipeline source state");
  throw "Bad pipeline source state";
}
