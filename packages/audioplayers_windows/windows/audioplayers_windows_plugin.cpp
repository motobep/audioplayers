#include "include/audioplayers_windows/audioplayers_windows_plugin.h"

// This must be included before many other Windows headers.
#include <windows.h>

// For getPlatformVersion; remove unless needed for your plugin implementation.
#include <VersionHelpers.h>
#include <flutter/encodable_value.h>
#include <flutter/event_channel.h>
#include <flutter/method_channel.h>
#include <flutter/plugin_registrar_windows.h>
#include <flutter/standard_method_codec.h>

#include <map>
#include <memory>
#include <sstream>

#include "audio_player.h"

namespace {

using namespace flutter;

template <typename T>
T GetArgument(const std::string arg, const EncodableValue* args, T fallback) {
  T result{fallback};
  const auto* arguments = std::get_if<EncodableMap>(args);
  if (arguments) {
    auto result_it = arguments->find(EncodableValue(arg));
    if (result_it != arguments->end()) {
      if (!result_it->second.IsNull())
        result = std::get<T>(result_it->second);
    }
  }
  return result;
}

template <typename T>
T GetArgumentOrFail(const std::string arg, const EncodableValue* args) {
  const auto* arguments = std::get_if<EncodableMap>(args);
  if (arguments) {
    auto result_it = arguments->find(EncodableValue(arg));
    if (result_it != arguments->end()) {
      if (!result_it->second.IsNull())
        return std::get<T>(result_it->second);
    }
  }
  printf("Bad arg: %s\n", arg.c_str());;
  throw "Bad arg";
}

class AudioplayersWindowsPlugin : public Plugin {
 public:
  static void RegisterWithRegistrar(PluginRegistrarWindows* registrar);

  AudioplayersWindowsPlugin();

  virtual ~AudioplayersWindowsPlugin();

 private:
  std::map<std::string, std::unique_ptr<AudioPlayer>> audioPlayers;

  static inline BinaryMessenger* binaryMessenger;
  static inline std::unique_ptr<MethodChannel<EncodableValue>> methods{};
  static inline std::unique_ptr<MethodChannel<EncodableValue>> globalMethods{};
  static inline std::unique_ptr<EventStreamHandler<>> globalEvents{};

  // Called when a method is called on this plugin's channel from Dart.
  void HandleMethodCall(const MethodCall<EncodableValue>& method_call,
                        std::unique_ptr<MethodResult<EncodableValue>> result);

  void HandleGlobalMethodCall(
      const MethodCall<EncodableValue>& method_call,
      std::unique_ptr<MethodResult<EncodableValue>> result);

  void CreatePlayer(std::string playerId);

  AudioPlayer* GetPlayer(std::string playerId);

  void OnGlobalLog(const std::string& message);
};

// static
void AudioplayersWindowsPlugin::RegisterWithRegistrar(
    PluginRegistrarWindows* registrar) {

  if (_putenv_s("GST_PLUGIN_PATH", D_GST_PLUGIN_PATH) == 0) {
	  printf("GST_PLUGIN_PATH set\n");
  } else {
	  perror("ERROR: GST_PLUGIN_PATH was not set\n");
  }

  binaryMessenger = registrar->messenger();
  methods = std::make_unique<MethodChannel<EncodableValue>>(
      binaryMessenger, "xyz.luan/audioplayers",
      &StandardMethodCodec::GetInstance());
  globalMethods = std::make_unique<MethodChannel<EncodableValue>>(
      binaryMessenger, "xyz.luan/audioplayers.global",
      &StandardMethodCodec::GetInstance());
  auto _globalEventChannel = std::make_unique<EventChannel<EncodableValue>>(
      binaryMessenger, "xyz.luan/audioplayers.global/events",
      &StandardMethodCodec::GetInstance());

  auto plugin = std::make_unique<AudioplayersWindowsPlugin>();

  methods->SetMethodCallHandler(
      [plugin_pointer = plugin.get()](const auto& call, auto result) {
        plugin_pointer->HandleMethodCall(call, std::move(result));
      });

  globalMethods->SetMethodCallHandler(
      [plugin_pointer = plugin.get()](const auto& call, auto result) {
        plugin_pointer->HandleGlobalMethodCall(call, std::move(result));
      });
  globalEvents = std::make_unique<EventStreamHandler<>>();
  auto _obj_stm_handle =
      static_cast<StreamHandler<EncodableValue>*>(globalEvents.get());
  std::unique_ptr<StreamHandler<EncodableValue>> _ptr{_obj_stm_handle};
  _globalEventChannel->SetStreamHandler(std::move(_ptr));

  registrar->AddPlugin(std::move(plugin));
}

AudioplayersWindowsPlugin::AudioplayersWindowsPlugin() {}

AudioplayersWindowsPlugin::~AudioplayersWindowsPlugin() {}

void AudioplayersWindowsPlugin::HandleGlobalMethodCall(
    const MethodCall<EncodableValue>& method_call,
    std::unique_ptr<MethodResult<EncodableValue>> result) {
  auto args = method_call.arguments();

  if (method_call.method_name().compare("setAudioContext") == 0) {
    this->OnGlobalLog("Setting AudioContext is not supported on Windows");
  } else if (method_call.method_name().compare("emitLog") == 0) {
    auto message = GetArgument<std::string>("message", args, std::string());
    this->OnGlobalLog(message);
  } else if (method_call.method_name().compare("emitError") == 0) {
    auto code = GetArgument<std::string>("code", args, std::string());
    auto message = GetArgument<std::string>("message", args, std::string());
    globalEvents->Error(code, message, nullptr);
    result->Success();
  } else {
    result->NotImplemented();
    return;
  }

  result->Success();
}

void AudioplayersWindowsPlugin::HandleMethodCall(
    const MethodCall<EncodableValue>& method_call,
    std::unique_ptr<MethodResult<EncodableValue>> result) {
  auto args = method_call.arguments();
printf("method name: '%s'\n", method_call.method_name().c_str());

  auto playerId = GetArgument<std::string>("playerId", args, std::string());
  if (playerId.empty()) {
    result->Error("WindowsAudioError",
                  "Call missing mandatory parameter playerId.", nullptr);
    return;
  }

  if (method_call.method_name().compare("create") == 0) {
	  printf("----- CREATE PLAYER -----\n");
    CreatePlayer(playerId);
    result->Success();
    return;
  }

  auto player = GetPlayer(playerId);
  if (!player) {
    result->Error(
        "WindowsAudioError",
        "Player has not yet been created or has already been disposed.",
        nullptr);
    return;
  }

  if (method_call.method_name().compare("pause") == 0) {
    player->Pause(); // +
  } else if (method_call.method_name().compare("resume") == 0) {
    player->Resume(); // +
  } else if (method_call.method_name().compare("stop") == 0) {
    player->Pause(); // +
	player->SetPosition(0);
  } else if (method_call.method_name().compare("release") == 0) {
    player->ReleaseMediaSource(); // +
  } else if (method_call.method_name().compare("seek") == 0) {
    auto positionInMs = GetArgument<int>(
        "position", args, (int)player->GetPosition().value_or(0)); // +
    player->SetPosition(positionInMs);
  } else if (method_call.method_name().compare("setSourceUrl") == 0) {
      printf(">>> setSourceUrl\n");
    auto url = GetArgument<std::string>("url", args, std::string()); // +

    if (url.empty()) {
      result->Error("WindowsAudioError", "Null URL received on setSourceUrl",
                    nullptr);
      return;
    }
    auto isLocal = GetArgument<bool>("isLocal", args, false);
      if (isLocal) {
        url = std::string("file://" "/") + url;
      }
      printf("<> setSourceUrl\n");
      player->SetSourceUrl(url);
      printf("<<< setSourceUrl\n");
  } else if (method_call.method_name().compare("setSourceBytes") == 0) {
      result->Error("WindowsAudioError", "Unimplemented setSourceBytes", nullptr); // +
      return;
  } else if (method_call.method_name().compare("setSourceByteStream") == 0) {
      player->SetSourceByteStream();
  } else if (method_call.method_name().compare("pushBuffer") == 0) {
      const guint8* buffer = GetArgumentOrFail<std::vector<uint8_t>>("buffer", args).data();
      int64_t len = GetArgumentOrFail<int64_t>("len", args);
      int64_t ok = player->PushBuffer(buffer, len);
      result->Success(EncodableValue(ok));
      return;
  } else if (method_call.method_name().compare("flushBuffers") == 0) {
      player->FlushBuffers();
  } else if (method_call.method_name().compare("getDuration") == 0) {
    auto optDuration = player->GetDuration(); // +
    result->Success(optDuration.has_value()
                        ? EncodableValue(optDuration.value())
			: EncodableValue(std::monostate{})
			);
    return;
  } else if (method_call.method_name().compare("setVolume") == 0) {
    auto volume = GetArgument<double>("volume", args, 1.0);
    player->SetVolume(volume);
  } else if (method_call.method_name().compare("getCurrentPosition") == 0) {
    auto optPosition = player->GetPosition(); // +
    result->Success(optPosition.has_value()
                        ? EncodableValue(optPosition.value())
                        : EncodableValue(std::monostate{})
			);
    return;
  } else if (method_call.method_name().compare("setPlaybackRate") == 0) {
    auto playbackRate = GetArgument<double>("playbackRate", args, 1.0); // +
    player->SetPlaybackRate(playbackRate);
  } else if (method_call.method_name().compare("setReleaseMode") == 0) {
    auto releaseMode =
        GetArgument<std::string>("releaseMode", args, std::string()); // +
    if (releaseMode.empty()) {
      result->Error("WindowsAudioError",
                    "Error calling setReleaseMode, releaseMode cannot be null",
                    nullptr);
      return;
    }
    auto looping = releaseMode.find("loop") != std::string::npos;
    player->SetLooping(looping);
  } else if (method_call.method_name().compare("setPlayerMode") == 0) {
    // windows doesn't have multiple player modes, so this should no-op
  } else if (method_call.method_name().compare("setBalance") == 0) {
    double balance = GetArgument<double>("balance", args, 0.0); // +
    player->SetBalance((float) balance);
  } else if (method_call.method_name().compare("equalizer.getEnabled") == 0) {
    result->Success(EncodableValue(player->GetEnabled()));
    return;
  } else if (method_call.method_name().compare("equalizer.setEnabled") == 0) {
    bool isEnabled = GetArgumentOrFail<bool>("isEnabled", args);
    player->SetEnabled(isEnabled);
  } else if (method_call.method_name().compare("equalizer.getNumberOfBands") == 0) {
    result->Success(EncodableValue(player->GetNumberOfBands()));
    return;
  } else if (method_call.method_name().compare("equalizer.getLimits") == 0) {
    result->Success(EncodableValue(player->GetLimits()));
    return;
  } else if (method_call.method_name().compare("equalizer.getBand") == 0) {
    int bandIndex = GetArgumentOrFail<int>("bandIndex", args);
    result->Success(EncodableValue(player->GetBand(bandIndex)));
    return;
  } else if (method_call.method_name().compare("equalizer.setBand") == 0) {
    int bandIndex = GetArgumentOrFail<int>("bandIndex", args);
    EncodableMap band = GetArgumentOrFail<EncodableMap>("band", args);
    player->SetBand(bandIndex, band);
  } else if (method_call.method_name().compare("emitLog") == 0) {
    auto message = GetArgument<std::string>("message", args, std::string()); // +
    player->OnLog(message);
  } else if (method_call.method_name().compare("emitError") == 0) {
    auto code = GetArgument<std::string>("code", args, std::string()); // +
    auto message = GetArgument<std::string>("message", args, std::string());
    player->OnError(code, message, nullptr, nullptr);
  } else if (method_call.method_name().compare("dispose") == 0) {
    player->Dispose(); // +
    audioPlayers.erase(playerId);
  } else {
  printf("else not implemented\n");
    result->NotImplemented();
    return;
  }
  printf("result->Success()\n");
  result->Success();
  printf("result->Success() after\n");
}

void AudioplayersWindowsPlugin::CreatePlayer(std::string playerId) {
  auto eventChannel = std::make_unique<EventChannel<EncodableValue>>(
      binaryMessenger, "xyz.luan/audioplayers/events/" + playerId,
      &StandardMethodCodec::GetInstance());

  auto eventHandler = new EventStreamHandler<>();
  auto _obj_stm_handle =
      static_cast<StreamHandler<EncodableValue>*>(eventHandler);
  std::unique_ptr<StreamHandler<EncodableValue>> _ptr{_obj_stm_handle};
  eventChannel->SetStreamHandler(std::move(_ptr));

  auto player =
      std::make_unique<AudioPlayer>(playerId, methods.get(), eventHandler);
  audioPlayers.insert(std::make_pair(playerId, std::move(player)));
}

AudioPlayer* AudioplayersWindowsPlugin::GetPlayer(std::string playerId) {
  auto searchPlayer = audioPlayers.find(playerId);
  if (searchPlayer == audioPlayers.end()) {
    return nullptr;
  }
  return searchPlayer->second.get();
}

void AudioplayersWindowsPlugin::OnGlobalLog(const std::string& message) {
  globalEvents->Success(std::make_unique<flutter::EncodableValue>(
      flutter::EncodableMap({{flutter::EncodableValue("event"),
                              flutter::EncodableValue("audio.onLog")},
                             {flutter::EncodableValue("value"),
                              flutter::EncodableValue(message)}})));
}

}  // namespace

void AudioplayersWindowsPluginRegisterWithRegistrar(
    FlutterDesktopPluginRegistrarRef registrar) {
  AudioplayersWindowsPlugin::RegisterWithRegistrar(
      PluginRegistrarManager::GetInstance()
          ->GetRegistrar<PluginRegistrarWindows>(registrar));
}
