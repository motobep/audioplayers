#include "audio_player.h"
#include "Logger.h"

void loop_func(GMainLoop **_g_main_loop_ptr) {
  logger.log("loop func");
  logger.log("main loop new");
  *_g_main_loop_ptr = g_main_loop_new(NULL, FALSE);
  GMainLoop *loop = *_g_main_loop_ptr;
  logger.log("loop p: %p", loop);
  logger.log(">>> main loop");
  g_main_loop_run(loop);
  logger.log("<<< main loop");
}

void AudioPlayer::thread_start() {
  logger.log("Thread start");
  _thread = std::thread(loop_func, &_g_main_loop);
}

void AudioPlayer::thread_end() {
  logger.log("Thread end");
  if (_g_main_loop != NULL) {
    logger.log("GLoop quit");
    g_main_loop_quit(_g_main_loop);
  } else {
    logger.log("---------\n[ERROR]: GLoop is NULL");
  }

  logger.log("Joining thread ...");
  _thread.join();
  logger.log("Joined");
}
