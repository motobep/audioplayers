#pragma once

#include <gst/pbutils/gstdiscoverer.h>
#include "audio_player_platform_specific.h"

#include <future>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>

// STL headers
#include <functional>

#include <vector>

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
  using SelfFunc = void (*)(AudioPlayer*);
  using OnSendSuccessFunc = void (*)(MyEventChannel* eventChannel,
                                     const std::string& event,
                                     const MyVariant& value);
  using OnErrorFunc = void (*)(MyEventChannel* eventChannel,
                               const std::string& code,
                               const std::string& message,
                               const char* details,
                               GError** error);

  AudioPlayer(std::string playerId,
              MyMethodChannel* methodChannel,
              MyEventChannel* eventChannel,
              SelfFunc onInitEndCallback,
              SelfFunc onDisposeEndCallback,
              OnSendSuccessFunc onSendSuccessCallback,
              OnErrorFunc onErrorCallback);

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
  std::map<std::string, std::vector<double>> GetLimits();

  std::map<std::string, double> GetBand(int bandIndex);
  void SetBand(int bandIndex, std::map<std::string, double> band);

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

  void OnError(const std::string& code,
               const std::string& message,
               const char* details,
               GError** error);

  void OnLog(const std::string& message);

  void SendSuccess(const std::string& event, const MyVariant& value);

  virtual ~AudioPlayer();

  std::string http_proxy{};

  bool isUsingEventChannel = true;

  GstStateChangeReturn SetPipelineState(GstState state);
  void printPipelineState(const char*);

 private:
  // Gst members
  GstDiscoverer* discoverer;

  GstElement* pipeline;
  GstElement* appsrc;
  GstElement* app_decodebin;
  GstElement* uridecodebin;
  GstElement* audioconvert;
  GstElement* audioresample;
  GstElement* volume_elem;
  GstElement* equalizer = nullptr;
  // GstElement* panorama = nullptr;
  GstElement* audiosink = nullptr;
  GstBus* bus = nullptr;

  bool _isSourceInitialized = false;
  bool _isPlaying = false;
  bool _isLooping = false;
  bool _isSeekCompleted = true;
  double _playbackRate = 1.0;
  guint _refreshId;

  std::string _url{};

 public:
  // Think about making private again
  std::string _playerId;
  MyEventChannel* _eventChannel;

 private:
  SelfFunc OnInitEndCallback = nullptr;
  SelfFunc OnDisposeEndCallback = nullptr;
  OnSendSuccessFunc OnSendSuccessCallback = nullptr;
  OnErrorFunc OnErrorCallback = nullptr;

  GObject* eqBands[__AUDIO_PLAYER_NUM_BUNDS];
  float eqWhenDisabledGains[__AUDIO_PLAYER_NUM_BUNDS];

  static const int eqNumBands = __AUDIO_PLAYER_NUM_BUNDS;
  std::map<std::string, std::vector<double>> _limitsMap{};
  bool _isEnabled = true;

  static gboolean OnBusMessage(GstBus* bus,
                               GstMessage* message,
                               AudioPlayer* data);

  static gboolean OnRefresh(AudioPlayer* data);

  void SetPlayback(int64_t seekTo, double rate);

  std::optional<int64_t> getDurationWithDiscoverer(std::string path);

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

#include "audio_player_definition_specific.h"
};
