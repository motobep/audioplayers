#pragma once

#include <flutter_linux/flutter_linux.h>

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
#include <gst/app/gstappsrc.h>
#include <gst/gst.h>
}

#define __AUDIO_PLAYER_NUM_BUNDS 10

typedef enum {
  SRC_STATE_APP = 0,
  SRC_STATE_URI = 1,
} SrcState;

class AudioPlayer {
 public:
  AudioPlayer(std::string playerId,
              FlMethodChannel* methodChannel,
              FlEventChannel* eventChannel);

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
  FlValue* GetLimits();

  FlValue* GetBand(int bandIndex);
  void SetBand(int bandIndex, FlValue* band);

  void SetBalance(float balance);

  void SetLooping(bool isLooping);

  void SetVolume(double volume);

  void SetPlaybackRate(double rate);

  void SetPosition(int64_t position);

  void SetSourceUrl(std::string url);

  void SetSourceByteStream();

  int64_t PushBuffer(const guint8* buffer, ssize_t len);

  void FlushBuffers(bool);

  void ReleaseMediaSource();

  void OnError(const gchar* code,
               const gchar* message,
               FlValue* details,
               GError** error);

  void OnLog(const gchar* message);

  virtual ~AudioPlayer();

  std::string http_proxy{};

  GstStateChangeReturn SetPipelineState(GstState state);
  void printPipelineState(const char*);

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

  bool _isSourceInitialized = false;
  bool _isPlaying = false;
  bool _isLooping = false;
  bool _isSeekCompleted = true;
  double _playbackRate = 1.0;
  guint _refreshId;

  std::string _url{};
  std::string _playerId;
  FlEventChannel* _eventChannel;

  GObject* eqBands[__AUDIO_PLAYER_NUM_BUNDS];
  float eqWhenDisabledGains[__AUDIO_PLAYER_NUM_BUNDS];

  static const int eqNumBands = __AUDIO_PLAYER_NUM_BUNDS;
  FlValue* _bandMap = fl_value_new_map();
  FlValue* _limitsMap = fl_value_new_map();
  bool _isEnabled = true;

  static gboolean OnBusMessage(GstBus* bus,
                               GstMessage* message,
                               AudioPlayer* data);

  static gboolean OnRefresh(AudioPlayer* data);

  void SetPlayback(int64_t seekTo, double rate);

  void flushBuffersHard();
  void flushBuffersSoft(bool);

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

  SrcState GetSrcState();
};
