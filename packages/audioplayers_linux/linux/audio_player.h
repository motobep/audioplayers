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

extern "C" {
#include <gst/gst.h>
}

#define __AUDIO_PLAYER_NUM_BUNDS 10

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

  void Stop();

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

  void ReleaseMediaSource();

  void OnError(const gchar* code,
               const gchar* message,
               FlValue* details,
               GError** error);

  void OnLog(const gchar* message);

  virtual ~AudioPlayer();

 private:
  // Gst members
  GstElement* playbin = nullptr;
  GstElement* source = nullptr;
  GstElement* equalizer = nullptr;
  GstElement* panorama = nullptr;
  GstElement* audiobin = nullptr;
  GstElement* audiosink = nullptr;
  GstPad* sinkPad = nullptr;
  GstBus* bus = nullptr;

  bool _isInitialized = false;
  bool _isPlaying = false;
  bool _isLooping = false;
  bool _isSeekCompleted = true;
  double _playbackRate = 1.0;

  std::string _url{};
  std::string _playerId;
  FlEventChannel* _eventChannel;

  GObject* eqBands[__AUDIO_PLAYER_NUM_BUNDS];

  static const int eqNumBands = __AUDIO_PLAYER_NUM_BUNDS;
  FlValue* _bandMap = fl_value_new_map();
  FlValue* _limitsMap = fl_value_new_map();

  static void SourceSetup(GstElement* playbin,
                          GstElement* source,
                          GstElement** p_src);

  static gboolean OnBusMessage(GstBus* bus,
                               GstMessage* message,
                               AudioPlayer* data);

  void SetPlayback(int64_t seekTo, double rate);

  void OnMediaError(GError* error, gchar* debug);

  void OnMediaStateChange(GstObject* src,
                          GstState* old_state,
                          GstState* new_state);

  void OnDurationUpdate();

  void OnSeekCompleted();

  void OnPlaybackEnded();

  void OnPrepared(bool isPrepared);

  void SetGain(int bandIndex, float value);
  void SetBandwidth(int bandIndex, float value);
  void SetFrequency(int bandIndex, float value);
};
