#pragma once

//#include <flutter_linux/flutter_linux.h>

#include <flutter/event_channel.h>
#include <flutter/event_stream_handler.h>
#include <flutter/event_stream_handler_functions.h>
#include <flutter/method_channel.h>
#include <flutter/plugin_registrar_windows.h>
#include <flutter/standard_method_codec.h>

#include "event_stream_handler.h"

#include <future>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>

// STL headers
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <string>

#include <cassert>

#define assertm(exp, msg) assert((void(msg), exp))

extern "C" {
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
}

#define __AUDIO_PLAYER_NUM_BUNDS 10

typedef flutter::EncodableValue _FlValue;
typedef flutter::EncodableMap _EncMap;

typedef uint64_t ssize_t;

typedef enum {
  SRC_STATE_APP = 0,
  SRC_STATE_URI = 1,
} SrcState;

class AudioPlayer {
 public:
 AudioPlayer::AudioPlayer(
	 std::string playerId,
	 flutter::MethodChannel<flutter::EncodableValue>* methodChannel,
	 EventStreamHandler<>* eventHandler);

  std::optional<int64_t> GetPosition();

  std::optional<int64_t> GetDuration();

  bool GetLooping();

  void Play();

  void Pause();

  void Resume();

  void Dispose();

  // Equalizer
  bool GetEnabled();
  void SetEnabled(bool isEnabled);

  int GetNumberOfBands();
  _EncMap GetLimits();

  _EncMap GetBand(int bandIndex);
  void SetBand(int bandIndex, _EncMap band);

  void SetBalance(float balance);

  void SetLooping(bool isLooping);

  void SetVolume(double volume);

  void SetPlaybackRate(double rate);

  void SetPosition(int64_t position);

  void SetSourceUrl(std::string url);

  void SetSourceByteStream();

  int64_t PushBuffer(const guint8* buffer, ssize_t len);

  void FlushBuffers();

  void ReleaseMediaSource();

  void OnError(const std::string& code,
		  const std::string& message,
		  const flutter::EncodableValue& details,
		  GError** error
		  );

  void OnLog(const std::string& message);

  virtual ~AudioPlayer();

  std::string http_proxy{};

 private:
  // Gst members
  GstElement* pipeline;
  GstElement* appsrc;
  GstElement* app_decodebin;
  GstElement* uridecodebin;
  GstElement* audioconvert;
  GstElement* audioresample;
  GstElement* volume_elem;
  GstElement* equalizer = nullptr;
  GstElement* panorama = nullptr;
  GstElement* audiosink = nullptr;
  GstBus* bus = nullptr;

  bool _isInitialized = false;
  bool _isPlaying = false;
  bool _isLooping = false;
  bool _isSeekCompleted = true;
  double _playbackRate = 1.0;
  guint _refreshId;

  std::string _url{};
  std::string _playerId;
  EventStreamHandler<>* _eventHandler;


  GMainLoop* _g_main_loop = nullptr;
  std::thread _thread;

  void thread_start();
  void thread_end();


  GObject* eqBands[__AUDIO_PLAYER_NUM_BUNDS];
  float eqWhenDisabledGains[__AUDIO_PLAYER_NUM_BUNDS];

  static const int eqNumBands = __AUDIO_PLAYER_NUM_BUNDS;
  _EncMap _limitsMap = _EncMap{};
  bool _isEnabled = true;

  static gboolean OnBusMessage(GstBus* bus,
                               GstMessage* message,
                               AudioPlayer* data);

  static gboolean OnRefresh(AudioPlayer* data);

  void SetPlayback(int64_t seekTo, double rate);

  void OnMediaError(GError* error, gchar* debug);

  void OnMediaStateChange(GstObject* src,
                          GstState* old_state,
                          GstState* new_state);

  void OnPositionUpdate();

  void OnDurationUpdate();

  void OnSeekCompleted();

  void OnPlaybackEnded();

  void OnPrepared(bool isPrepared);

  void SetGain(int bandIndex, float value);
  void SetBandwidth(int bandIndex, float value);
  void SetFrequency(int bandIndex, float value);

  GstStateChangeReturn SetPipelineState(GstState state);

  SrcState GetSrcState();
};
