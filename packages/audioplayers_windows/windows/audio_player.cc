#include "audio_player.h"
//#include <flutter_linux/flutter_linux.h>
extern "C" {
#include <gst/gst.h>
}

#define STR_LINK_TROUBLESHOOTING \
  "https://github.com/bluefireteam/audioplayers/blob/main/troubleshooting.md"

#define TIMOUT_CLOCK_TIME (100 * 1000000)  // 100 ms
#define APP_REFRESH_TIME (250)             // ms


template<typename T>
flutter::EncodableList arrayToEncList(const T* arr, int size) {
	flutter::EncodableList list;
	for (int i = 0; i < size; i++) {
		list.push_back(flutter::EncodableValue(arr[i]));
	}
	return list;
}

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
  printf("----------\non_pad_added_from_src()\n-------\n");
  GstElement* audioconvert = (GstElement*)udata;
  GstCaps* caps = gst_pad_get_current_caps(pad);
  GstStructure* s = gst_caps_get_structure(caps, 0);
  const gchar* name = gst_structure_get_name(s);

  bool has_prefix = g_str_has_prefix(name, "audio/");
  gst_caps_unref(caps);
  if (!has_prefix) {
    return;
  }

  GstPad* sinkpad = gst_element_get_static_pad(audioconvert, "sink");
  if (gst_pad_is_linked(sinkpad)) {
    printf("Pad already linked\n");
    gst_object_unref(sinkpad);
    return;
  }

  if (gst_pad_link(pad, sinkpad) != GST_PAD_LINK_OK) {
    perror("Failed to link pad to audioconvert\n");
  }
  gst_object_unref(sinkpad);
}

void on_source_setup(GstElement* bin, GstElement* source, GstElement* udata) {
  printf("---\nnon_source_setup()\n");
  AudioPlayer* player = (AudioPlayer*)udata;
  GParamSpec* has_prop =
      g_object_class_find_property(G_OBJECT_GET_CLASS(source), "proxy");
  if (has_prop != 0) {
    // printf("has prop\n");
    gchararray proxy;
    // g_object_get(G_OBJECT(source), "proxy", &proxy, NULL);
    // printf("proxy before: '%s'\n", proxy);
    if (!player->http_proxy.empty()) {
      g_object_set(G_OBJECT(source), "proxy", player->http_proxy.c_str(), NULL);
    } else {
      printf("http_proxy is empty\n");
    }

    g_object_get(G_OBJECT(source), "proxy", &proxy, NULL);
    printf("proxy after: '%s'\n", proxy);
  } else {
    printf("\n-------\nno prop\n");
  }
};

static void on_need_data(GstElement* appsrc, guint length, gpointer udata) {
  printf("\nneed-data (length=%u)\n", length);
}

static void on_enough_data(GstElement* appsrc, gpointer udata) {
  printf("\nenough-data\n");
}

AudioPlayer::AudioPlayer(
		std::string playerId,
		flutter::MethodChannel<flutter::EncodableValue>* methodChannel,
		EventStreamHandler<>* eventHandler)
	: _playerId(playerId),
	_eventHandler(eventHandler) {
  setvbuf(stdout, NULL, _IONBF, 0);

  // GStreamer lib only needs to be initialized once, but doing it while
  // registering the plugin can be problematic as it likely needs a GUI to be
  // present. Calling it multiple times is fine.
  gst_init(NULL, NULL);

  this->pipeline = gst_pipeline_new("pipeline");

  // Bytes
  appsrc = gst_element_factory_make("appsrc", "appsrc");
  app_decodebin = gst_element_factory_make("decodebin", "app_decodebin");
  // OR Uri
  uridecodebin = gst_element_factory_make("uridecodebin", "uridecodebin");

  audioconvert = gst_element_factory_make("audioconvert", "audioconvert");
  audioresample = gst_element_factory_make("audioresample", "audioresample");
  volume_elem = gst_element_factory_make("volume", "volume_elem");
  equalizer = gst_element_factory_make("equalizer-nbands", "equalizer");
  // panorama = gst_element_factory_make("audiopanorama", NULL);
  audiosink = gst_element_factory_make("autoaudiosink", NULL);

  // Setup equalizer and stereo balance controller
  if (!pipeline || !appsrc || !app_decodebin || !uridecodebin ||
      !audioconvert || !audioresample || !volume_elem || !equalizer 
      // || !panorama 
      || !audiosink) {
    perror("Failed to create elements\n");
    throw "Failed to create elements\n";
  }

  gst_bin_add_many(GST_BIN(pipeline), uridecodebin, audioconvert, audioresample,
                   volume_elem, equalizer, 
		   // panorama,
		   audiosink, NULL);

  if (!gst_element_link_many(audioconvert, audioresample, volume_elem,
                             equalizer, 
			     // panorama,
			     audiosink, NULL)) {
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
  // g_object_set(G_OBJECT(panorama), "method", 1, NULL);
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

  _EncMap& map = _limitsMap;
  map.insert({ _FlValue("zero"), _FlValue() });
  map.insert({ _FlValue("gain"), _FlValue(arrayToEncList(gainLimits, 2)) });
  map.insert({ _FlValue("bandwidth"), _FlValue(arrayToEncList(bandwidthLimits, 2))});
  map.insert({ _FlValue("frequency"), _FlValue(arrayToEncList(frequencyLimits, 2))});

  bus = gst_element_get_bus(pipeline);

  // Watch bus messages for one time events
  gst_bus_add_watch(bus, (GstBusFunc)AudioPlayer::OnBusMessage, this);

  // Refresh continuously to emit reoccurring events
  _refreshId = g_timeout_add(APP_REFRESH_TIME,
                             (GSourceFunc)AudioPlayer::OnRefresh, this);
  printf("_refreshId: %u\n", _refreshId);

  thread_start();
}

void loop_func(GMainLoop **_g_main_loop_ptr) {
  printf("loop func\n");
  printf("main loop new\n");
  *_g_main_loop_ptr = g_main_loop_new(NULL, FALSE);
GMainLoop *loop = *_g_main_loop_ptr;
  printf("loop p: %p\n", loop);
  printf(">>> main loop\n");
  g_main_loop_run(loop);
  printf("<<< main loop\n");
}

void AudioPlayer::thread_start() {
  printf("Thread start\n");
  _thread = std::thread(loop_func, &_g_main_loop);
}

void AudioPlayer::thread_end() {
  printf("Thread end\n");
  if (_g_main_loop != NULL) {
    printf("GLoop quit\n");
    g_main_loop_quit(_g_main_loop);
  } else {
    printf("---------\n[ERROR]: GLoop is NULL\n");
  }

  printf("Joining thread ...\n");
  _thread.join();
  printf("Joined\n");
}

AudioPlayer::~AudioPlayer() {}

void AudioPlayer::SetSourceUrl(std::string url) {
  printf("SetSourceSourceUrl\n");

  SrcState srcState = GetSrcState();
  if (srcState == SRC_STATE_APP) {
    // Stop pipeline
    SetPipelineState(GST_STATE_NULL);
    
    // Unset urldecodebin
    printf("Unset appsrc\n");
    // gst_element_unlink(appsrc, app_decodebin);
    // gst_element_unlink(app_decodebin, audioconvert);
    gst_bin_remove(GST_BIN(pipeline), appsrc);
    // Ref once more. Just because.
    gst_object_ref(app_decodebin);
    if (!gst_bin_remove(GST_BIN(pipeline), app_decodebin)) {
      printf("Can't remove app_decodebin\n");
      // throw "Can't remove app_decodebin\n";
    }

    gst_bin_add(GST_BIN(pipeline), uridecodebin);
  }

  if (_url != url) {
    _url = url;
    SetPipelineState(GST_STATE_NULL);
    _isInitialized = false;
    _isPlaying = false;
    if (!_url.empty()) {
      g_object_set(GST_OBJECT(uridecodebin), "uri", _url.c_str(), NULL);
      if (pipeline->current_state != GST_STATE_READY) {
        GstStateChangeReturn ret = SetPipelineState(GST_STATE_READY);
        if (ret == GST_STATE_CHANGE_FAILURE) {
          throw "Unable to set the pipeline to GST_STATE_READY.";
        }
      }
    }
  } else {
    this->OnPrepared(true);
  }

  printf("Switched to url: %s\n", url.c_str());
}

void AudioPlayer::SetSourceByteStream() {
  printf("SetSourceByteStream\n");

  SrcState srcState = GetSrcState();
  if (srcState == SRC_STATE_URI) {
    // Stop pipeline
    SetPipelineState(GST_STATE_NULL);

    // Unset urldecodebin
    printf("Unset uriSrc\n");
    // gst_element_unlink(uridecodebin, audioconvert);
    gst_bin_remove(GST_BIN(pipeline), uridecodebin);

    printf("Is null\n");
    if (app_decodebin == NULL) {
      printf("app_decodebin is NULL\n");
    }
    printf("Is element\n");
    if (!GST_IS_ELEMENT(app_decodebin)) {
      printf("app_decodebin is not GstElement\n");
    }

    printf("Adding appsrc\n");
    gst_bin_add(GST_BIN(pipeline), appsrc);
    printf("Adding app_decodebin\n");
    if (!gst_bin_add(GST_BIN(pipeline), app_decodebin)) {
      printf("Can't add app_decodebin\n");
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
    printf("Switched to appsrc\n");
  } else {
    this->OnPrepared(true);
  }
}

int64_t AudioPlayer::PushBuffer(const guint8* buffer, ssize_t len) {
  // printf("Buffer's len (%ld)\n", len);
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
    g_signal_emit_by_name(appsrc, "end-of-stream", NULL);
  } else if (len < 0) {
    g_printerr("Read error: %s\n", strerror(errno));
  }
  return 0;
}

void AudioPlayer::FlushBuffers() {
  printf("\n----\nFlushing with events\n");
  GstEvent* flush_start_event = gst_event_new_flush_start();
  gst_element_send_event(pipeline, flush_start_event);

  GstEvent* flush_stop_event = gst_event_new_flush_stop(TRUE);
  gst_element_send_event(pipeline, flush_stop_event);
  printf("Flushed\n");
}

void AudioPlayer::ReleaseMediaSource() {
  printf("ReleaseMediaSource\n");
  if (_isPlaying)
    _isPlaying = false;
  if (_isInitialized)
    _isInitialized = false;
  _url.clear();

  GstState pipelineState;
  printf("ReleaseMediaSource: gst_element_get_state\n");
  GstStateChangeReturn ret =
      gst_element_get_state(pipeline, &pipelineState, NULL, TIMOUT_CLOCK_TIME);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    printf("ReleaseMediaSource failed\n");
  } else {
    // printf("ReleaseMediaSource:\tout\n");
    if (ret != GST_STATE_CHANGE_SUCCESS) {
      printf("ReleaseMediaSource not SUCCESS (%u)\n", ret);
    }
    if (pipelineState > GST_STATE_NULL) {
      SetPipelineState(GST_STATE_NULL);
    }
  }
}

gboolean AudioPlayer::OnBusMessage(GstBus* bus,
                                   GstMessage* message,
                                   AudioPlayer* data) {
  printf("OnBusMessage (%d)\n", GST_MESSAGE_TYPE(message));
  switch (GST_MESSAGE_TYPE(message)) {
    case GST_MESSAGE_ERROR: {
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
      data->OnPlaybackEnded();
      break;
    case GST_MESSAGE_DURATION_CHANGED:
      data->OnDurationUpdate();
      break;
    case GST_MESSAGE_ASYNC_DONE:
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
  // printf("OnRefresh()\n");
  if (data->pipeline == nullptr) {
    return FALSE;
  }
  // We do not want to update anything unless we are in PLAYING state
  GstState pipelineState;
  // printf("OnRefresh: gst_element_get_state\n");
  GstStateChangeReturn ret = gst_element_get_state(
      data->pipeline, &pipelineState, NULL, TIMOUT_CLOCK_TIME);
  if (ret != GST_STATE_CHANGE_SUCCESS) {
    // printf("OnRefresh not SUCCESS (%u)\n", ret);
  }
  if (ret == GST_STATE_CHANGE_FAILURE) {
    printf("OnRefresh failed\n");
  } else {
    // printf("OnRefresh:\tout\n");
    if (pipelineState == GST_STATE_PLAYING) {
      data->OnPositionUpdate();
    }
  }
  return TRUE;
}

void AudioPlayer::OnMediaError(GError* error, gchar* debug) {
  if (this->_eventHandler) {
    gchar const* code = "LinuxAudioError";
    gchar const* message;
    auto detailsStr = std::string(error->message) + " (Domain: " +
                      std::string(g_quark_to_string(error->domain)) +
                      ", Code: " + std::to_string(error->code) + ")";
    _FlValue details = _FlValue(detailsStr.c_str());
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

void AudioPlayer::OnError(const std::string& code,
		const std::string& message,
		const flutter::EncodableValue& details,
		GError** error // TODO: handle arg
		) {
  if (this->_eventHandler) {
    this->_eventHandler->Error(code, message, details);
  }
}

void AudioPlayer::OnMediaStateChange(GstObject* src,
                                     GstState* old_state,
                                     GstState* new_state) {
  // printf("OnMediaStateChange (%d -> %d)\n", *old_state, *new_state);
  if (!pipeline) {
    this->OnError("LinuxAudioError",
                  "Player was already disposed (OnMediaStateChange).", nullptr,
                  nullptr);
    return;
  }

  if (src == GST_OBJECT(pipeline)) {
    if (*new_state == GST_STATE_READY) {
      // Need to set to pause state, in order to make player functional
      // printf("Gstreamer: Need pause\n");
      GstStateChangeReturn ret = SetPipelineState(GST_STATE_PAUSED);
      if (ret == GST_STATE_CHANGE_FAILURE) {
        gchar const* errorDescription =
            "Unable to set the pipeline from GST_STATE_READY to "
            "GST_STATE_PAUSED.";
        if (this->_isInitialized) {
          this->OnError("LinuxAudioError", errorDescription, nullptr, nullptr);
        } else {
          this->OnError("LinuxAudioError",
                        "Failed to set source. For troubleshooting, "
                        "see: " STR_LINK_TROUBLESHOOTING,
                        _FlValue(std::string(errorDescription)), nullptr);
        }
      }
      if (this->_isInitialized) {
        this->_isInitialized = false;
      }
    } else if (*old_state == GST_STATE_PAUSED &&
               *new_state == GST_STATE_PLAYING) {
      OnPositionUpdate();
      OnDurationUpdate();
    } else if (*new_state >= GST_STATE_PAUSED) {
      if (!this->_isInitialized) {
        this->_isInitialized = true;
        this->OnPrepared(true);
        if (this->_isPlaying) {
          Resume();
        }
      }
    } else if (this->_isInitialized) {
      this->_isInitialized = false;
    }
  }
}

void AudioPlayer::OnPrepared(bool isPrepared) {
  if (this->_eventHandler) {
    this->_eventHandler->Success(std::make_unique<flutter::EncodableValue>(
        flutter::EncodableMap({{flutter::EncodableValue("event"),
                                flutter::EncodableValue("audio.onPrepared")},
                               {flutter::EncodableValue("value"),
                                flutter::EncodableValue(isPrepared)}})));
  }
}

void AudioPlayer::OnPositionUpdate() {
  if (this->_eventHandler) {
	int64_t position = GetPosition().value_or(0);
    this->_eventHandler->Success(
        std::make_unique<flutter::EncodableValue>(flutter::EncodableMap(
            {{flutter::EncodableValue("event"),
              flutter::EncodableValue("audio.onCurrentPosition")},
             {flutter::EncodableValue("value"), flutter::EncodableValue(position) }
		  })));
  }
}

void AudioPlayer::OnDurationUpdate() {
  if (this->_eventHandler) {
	int64_t duration = GetDuration().value_or(0);
    this->_eventHandler->Success(
        std::make_unique<flutter::EncodableValue>(flutter::EncodableMap(
            {{flutter::EncodableValue("event"),
              flutter::EncodableValue("audio.onDuration")},
             {flutter::EncodableValue("value"), flutter::EncodableValue(duration) }
		  })));
  }
}

void AudioPlayer::OnSeekCompleted() {
  if (this->_eventHandler) {
    OnPositionUpdate();
    this->_eventHandler->Success(
        std::make_unique<flutter::EncodableValue>(flutter::EncodableMap(
            {{flutter::EncodableValue("event"),
              flutter::EncodableValue("audio.onSeekComplete")},
             {flutter::EncodableValue("value"),
              flutter::EncodableValue(true)}})));
  }
}

void AudioPlayer::OnPlaybackEnded() {
  if (this->_eventHandler) {
    this->_eventHandler->Success(std::make_unique<flutter::EncodableValue>(
        flutter::EncodableMap({{flutter::EncodableValue("event"),
                                flutter::EncodableValue("audio.onComplete")},
                               {flutter::EncodableValue("value"),
                                flutter::EncodableValue(true)}})));
  }
  if (GetLooping()) {
    Play();
  } else {
    Pause();
    SetPosition(0);
  }
}

void AudioPlayer::OnLog(const std::string& message) {
  printf("OnLog\n");
  if (_eventHandler == nullptr) {
    printf("_eventHandler is NULL\n");
    return;
  }
  this->_eventHandler->Success(std::make_unique<flutter::EncodableValue>(
      flutter::EncodableMap({{flutter::EncodableValue("event"),
                              flutter::EncodableValue("audio.onLog")},
                             {flutter::EncodableValue("value"),
                              flutter::EncodableValue(message)}})));
  printf("OnLog return\n");
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
        eqWhenDisabledGains[i] = (float) gain;
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

_EncMap AudioPlayer::GetLimits() {
  return _limitsMap;
}

_EncMap AudioPlayer::GetBand(int bandIndex) {
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

  _EncMap map = _EncMap({
		  {_FlValue("gain"), _FlValue(gain)},
		  {_FlValue("bandwidth"), _FlValue(bandwidth)},
		  {_FlValue("frequency"), _FlValue(freq)}
		  });

  return map;
}


template <typename T>
T mapAt(const _EncMap& map, const std::string arg) {
	const auto& value = map.at(flutter::EncodableValue(arg));
	return std::get<T>(value);
}
bool mapHas(const _EncMap& map, const std::string arg) {
	return map.find(flutter::EncodableValue(arg)) != map.end();
}

void AudioPlayer::SetBand(int bandIndex, _EncMap band) {
  if (!equalizer) {
    this->OnLog("Equalizer was not initialized");
    return;
  }

  const auto& map = band;

  if (mapHas(map, "gain")) {
    double value = mapAt<double>(map, "gain");
    SetGain(bandIndex, (float) value);
  }

  if (mapHas(map, "bandwidth")) {
    double value = mapAt<double>(map, "bandwidth");
    SetBandwidth(bandIndex, (float) value);
  }

  if (mapHas(map, "frequency")) {
    double value = mapAt<double>(map, "frequency");
    SetFrequency(bandIndex, (float) value);
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
	throw "Not implemented panorama";
  // if (balance > 1.0f) {
  //   balance = 1.0f;
  // } else if (balance < -1.0f) {
  //   balance = -1.0f;
  // }
  // g_object_set(G_OBJECT(panorama), "panorama", balance, NULL);
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
  printf("SetPlayback\n");
  if (rate != 0 && _playbackRate != rate) {
    _playbackRate = rate;
  }

  if (!_isInitialized) {
    return;
  }
  printf("is initialized\n");
  // See:
  // https://gstreamer.freedesktop.org/documentation/tutorials/basic/playback-speed.html?gi-language=c
  if (!_isSeekCompleted) {
    return;
  }
  printf("seek completed\n");
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
    printf("SetPlayback NO boy\n");
    int64_t prevPos = GetPosition().value_or(-1);
    this->OnLog((std::string("Could not set playback to position ") +
                 std::to_string(position) + std::string(" and rate ") +
                 std::to_string(rate) + std::string(" prevPos=(") +
                 std::to_string(prevPos) + std::string(")."))
                    .c_str());
    _isSeekCompleted = true;
  } else {
    printf("Set to position: %lld\n", position);
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
  if (_isPlaying) {
    _isPlaying = false;
  }
  if (!_isInitialized) {
    return;
  }
  GstStateChangeReturn ret = SetPipelineState(GST_STATE_PAUSED);
  if (ret == GST_STATE_CHANGE_SUCCESS) {
    OnPositionUpdate();  // Update to exact position when pausing
  } else if (ret == GST_STATE_CHANGE_FAILURE) {
    throw "Unable to set the pipeline to GST_STATE_PAUSED.";
  }
}

void AudioPlayer::Resume() {
  // printf("Resume\n");
  if (!_isPlaying) {
    _isPlaying = true;
  }
  if (!_isInitialized) {
    return;
  }
  GstStateChangeReturn ret = SetPipelineState(GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_SUCCESS) {
    // Update position and duration when start playing, as no event is emitted
    // elsewhere
    OnPositionUpdate();
    OnDurationUpdate();
  } else if (ret == GST_STATE_CHANGE_FAILURE) {
    throw "Unable to set the pipeline to GST_STATE_PLAYING.";
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
  // bin_remove_and_null(GST_BIN(pipeline), &panorama);
  bin_remove_and_null(GST_BIN(pipeline), &audiosink);

  gst_object_unref(GST_OBJECT(pipeline));
  // Do not dispose method channel as it is used by multiple players!
  _eventHandler = nullptr;
  pipeline = nullptr;

  thread_end();
}

GstStateChangeReturn AudioPlayer::SetPipelineState(GstState state) {
  return gst_element_set_state(pipeline, state);
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
