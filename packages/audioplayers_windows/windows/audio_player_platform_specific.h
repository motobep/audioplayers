#include <flutter/event_channel.h>
#include <flutter/event_stream_handler.h>
#include <flutter/event_stream_handler_functions.h>
#include <flutter/method_channel.h>
#include <flutter/plugin_registrar_windows.h>
#include <flutter/standard_method_codec.h>

#include "event_stream_handler.h"

typedef flutter::EncodableValue _FlValue;
typedef flutter::EncodableValue MyVariant;

typedef flutter::MethodChannel<_FlValue> MyMethodChannel;
typedef EventStreamHandler<> MyEventChannel;

typedef uint64_t ssize_t;

