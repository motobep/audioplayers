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
#include "Logger.h"

namespace {

using namespace flutter;

Logger logger{};

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
		assert(!std::holds_alternative<int32_t>(result_it->second));
		if (result_it != arguments->end()) {
			if (!result_it->second.IsNull()) {
				return std::get<T>(result_it->second);
			}
		}
	}
	logger.log("Bad arg: %s", arg.c_str());
	throw "Bad arg";
}

int64_t GetLongValueOrFail(const std::string arg, const EncodableValue* args) {
	const auto* arguments = std::get_if<EncodableMap>(args);
	if (arguments) {
		auto result_it = arguments->find(EncodableValue(arg));
		if (result_it != arguments->end()) {
			if (!result_it->second.IsNull()) {
				if (std::holds_alternative<int32_t>(result_it->second)) {
					return std::get<int32_t>(result_it->second);
				}
				return  std::get<int64_t>(result_it->second);
			}
		}
	}
  logger.log("Bad arg: %s", arg.c_str());
  throw "Bad arg";
}

EncodableValue mapVectorDoubleToEncodableValue(const std::map<std::string, std::vector<double>>& data) {
  EncodableMap encoded_map;
  for (const auto& entry : data) {
    EncodableList encoded_list;
    for (double value : entry.second) {
      encoded_list.push_back(EncodableValue(value));
    }
    encoded_map[EncodableValue(entry.first)] = EncodableValue(encoded_list);
  }
  return EncodableValue(encoded_map);
}

EncodableValue mapDoubleToEncodableValue(const std::map<std::string, double>& data) {
  EncodableMap encoded_map;
  for (const auto& entry : data) {
    encoded_map[EncodableValue(entry.first)] = EncodableValue(entry.second);
  }
  return EncodableValue(encoded_map);
}

std::map<std::string, double> encodableMapToMapDouble(EncodableMap encodableMap) {
    std::map<std::string, double> map{};
    for (const auto& [key_var, value_var] : encodableMap) {
      if (const std::string* key = std::get_if<std::string>(&key_var)) {
	 if (const double* d = std::get_if<double>(&value_var)) {
           map[*key] = *d;
         }
      }
    }
    return map;
}

void loop_func(GMainLoop **g_main_loop_ptr) {
  logger.log("loop func");
  logger.log("main loop new");
  *g_main_loop_ptr = g_main_loop_new(NULL, FALSE);
  GMainLoop *loop = *g_main_loop_ptr;
  logger.log("loop p: %p", loop);
  logger.log(">>> main loop");
  g_main_loop_run(loop);
  logger.log("<<< main loop");
}

std::map<const std::string, std::pair<std::thread, GMainLoop*>> threadsPool{};

//bool has_thread = false;
void thread_start(const std::string id) {
	logger.log("Thread start id=%s", id.c_str());
	// Create thread, g_main_loop
	auto pair = threadsPool.find(id);
	if (pair != threadsPool.end()) {
		logger.error("Id=%s already exists", id.c_str());
		return;
	}
	// if (has_thread) {
	// 	logger.log("has thread already, return");
	// 	return;
	// }
	//has_thread = true;

	GMainLoop* g_main_loop = nullptr;
	std::thread thread = std::thread(loop_func, &g_main_loop);
	auto threadWithGLoop = std::make_pair(std::move(thread), std::move(g_main_loop));
	threadsPool.insert({id, std::move(threadWithGLoop)});
}

void thread_end(std::string id) {
	// Delete thread, g_main_loop
  logger.log("Thread end id=%s", id.c_str());

  auto keyValue = threadsPool.find(id);
	if (keyValue == threadsPool.end()) {
		logger.error("No thread with id: %s", id.c_str());
    return;
  }
	auto& threadWithGLoop = keyValue->second;
	std::thread& thread = threadWithGLoop.first;
	GMainLoop* g_main_loop = threadWithGLoop.second;
	if (g_main_loop != NULL) {
		logger.log("GLoop quit");
		g_main_loop_quit(g_main_loop);
		g_main_loop_unref(g_main_loop);
	} else {
		logger.error("---------\n[ERROR]: GLoop is NULL");
	}

	logger.log("Joining thread ...");
	thread.join();
	logger.log("Joined");

	threadsPool.erase(id);
}

void OnInitEndCallback(AudioPlayer* player) {
	std::string id = player->_playerId;
	thread_start(id);
}

void OnDisposeEndCallback(AudioPlayer* player) {
	player->_eventChannel = nullptr;

	std::string id = player->_playerId;
	thread_end(id);
}

void OnSendSuccessCallback(MyEventChannel* eventChannel,
		const std::string& event,
		const MyVariant& value) {
	eventChannel->Success(
			std::make_unique<_FlValue>(
				flutter::EncodableMap{
				{_FlValue("event"), _FlValue(event.c_str())},
				{_FlValue("value"), value}
				}
				)
			);
}

void OnErrorCallback(MyEventChannel* eventChannel,
		const std::string& code,
		const std::string& message,
		const char* details,
		GError** error) {
	  if (details != nullptr) {
		  _FlValue detailsFlValue = _FlValue(details);
		  eventChannel->Error(code, message, detailsFlValue);
	  } else {
		  eventChannel->Error(code, message, nullptr);
	  }
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
  logger.log("method name: '%s'", method_call.method_name().c_str());

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

	if (method_call.method_name().compare("setHttpProxy") == 0) {
		logger.log("setHttpProxy");
    auto http_proxy = GetArgument<std::string>("http_proxy", args, std::string()); // +
		logger.log("http_proxy: %s", http_proxy.c_str());
		player->http_proxy = http_proxy;
	} else if (method_call.method_name().compare("pause") == 0) {
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
      logger.log(">>> setSourceUrl");
      player->SetSourceUrl(url);
      logger.log("<<< setSourceUrl");
  } else if (method_call.method_name().compare("setSourceBytes") == 0) {
      result->Error("WindowsAudioError", "Unimplemented setSourceBytes", nullptr); // +
      return;
  } else if (method_call.method_name().compare("setSourceByteStream") == 0) {
      player->SetSourceByteStream();
  } else if (method_call.method_name().compare("pushBuffer") == 0) {
      const guint8* buffer = GetArgumentOrFail<std::vector<uint8_t>>("buffer", args).data();
      int64_t len = GetLongValueOrFail("len", args);
      int64_t ok = player->PushBuffer(buffer, len);
      result->Success(EncodableValue(ok));
      return;
  } else if (method_call.method_name().compare("flushBuffers") == 0) {
      player->FlushBuffers(true);
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
    EncodableValue val = mapVectorDoubleToEncodableValue(player->GetLimits());
    result->Success(val);
    return;
  } else if (method_call.method_name().compare("equalizer.getBand") == 0) {
    int bandIndex = GetArgumentOrFail<int>("bandIndex", args);
    EncodableValue val = mapDoubleToEncodableValue(player->GetBand(bandIndex));
    result->Success(val);
    return;
  } else if (method_call.method_name().compare("equalizer.setBand") == 0) {
    int bandIndex = GetArgumentOrFail<int>("bandIndex", args);
    EncodableMap band = GetArgumentOrFail<EncodableMap>("band", args);
    auto map = encodableMapToMapDouble(band);
    player->SetBand(bandIndex, map);
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
      std::make_unique<AudioPlayer>(playerId, methods.get(), eventHandler,
					OnInitEndCallback, OnDisposeEndCallback, OnSendSuccessCallback, OnErrorCallback);
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
