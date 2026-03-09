package xyz.luan.audioplayers.player

import xyz.luan.audioplayers.Logger
import android.content.Context
import android.media.AudioManager
import xyz.luan.audioplayers.AudioContextAndroid
import xyz.luan.audioplayers.AudioplayersPlugin
import xyz.luan.audioplayers.EventHandler
import xyz.luan.audioplayers.ReleaseMode
import xyz.luan.audioplayers.source.Source
import kotlin.math.min

class WrappedPlayer internal constructor(
    private val ref: AudioplayersPlugin,
    val eventHandler: EventHandler,
    var context: AudioContextAndroid,
) {
    var player: PlayerWrapper? = null

    init {
        createPlayer().also {
            player = it
        }
    }

    var source: Source? = null
        set(value) {
            logger.blue("source = $value")
            if (field != value) {
                field = value
                prepared = false
                if (value != null) {
                    released = false
                    logger.blue("setSource($value)")
                    player?.setSource(value)
                    player?.configAndPrepare()
                } else {
                    released = true
                    playing = false
                    player?.release()
                }
            } else {
                ref.handlePrepared(this, true)
            }
        }

    var volume = 1.0f
        set(value) {
            if (field != value) {
                field = value
                if (!released) {
                    player?.setVolumeAndBalance(value, balance)
                }
            }
        }

    var balance = 0.0f
        set(value) {
            if (field != value) {
                field = value
                if (!released) {
                    player?.setVolumeAndBalance(volume, value)
                }
            }
        }

    var rate = 1.0f
        set(value) {
            if (field != value) {
                field = value
                if (playing) {
                    player?.setRate(value)
                }
            }
        }

    // var releaseMode = ReleaseMode.RELEASE
    var releaseMode = ReleaseMode.STOP
        set(value) {
            if (field != value) {
                field = value
                if (!released) {
                    player?.setLooping(isLooping)
                }
            }
        }

    val isLooping: Boolean
        get() = releaseMode == ReleaseMode.LOOP

    var released = true

    var prepared: Boolean = false
        set(value) {
            if (field != value) {
                field = value
                ref.handlePrepared(this, value)
            }
        }

    var playing = false
    var shouldSeekTo = -1

    private val focusManager = FocusManager(this)
    /* private val focusManager = FocusManager.create(
        this,
        onGranted = {
            // Check if in playing state, as the focus can also be gained e.g. after a phone call, even if not playing.
            if (playing) {
                player?.start()
            }
        },
        onLoss = { isTransient ->
            if (isTransient) {
                // Do not check or set playing state, as the state should be recovered after granting focus again.
                player?.pause()
            } else {
                // Audio focus won't be recovered
                pause()
            }
        },
    ) */

    fun updateAudioContext(audioContext: AudioContextAndroid) {
        logger.log("updateAudioContext()")
        if (context == audioContext) {
            return
        }
        if (context.audioFocus != AudioManager.AUDIOFOCUS_NONE &&
            audioContext.audioFocus == AudioManager.AUDIOFOCUS_NONE
        ) {
            focusManager.handleStop()
        }
        this.context = audioContext.copy()

        // AudioManager values are set globally
        audioManager.mode = context.audioMode
        audioManager.isSpeakerphoneOn = context.isSpeakerphoneOn

        player?.let { p ->
            p.stop()
            prepared = false
            // Context is only applied, once the player.reset() was called
            p.updateContext(context)
            source?.let {
                p.setSource(it)
                p.configAndPrepare()
            }
        }
    }

    // Getters

    /**
     * Returns the duration of the media in milliseconds, if available.
     */
    fun getDuration(): Int? {
        logger.blue("getDuration(): playing=$playing, released=$released, prepared=$prepared")
        return if (prepared) player?.getDuration() else null
    }

    /**
     * Returns the current position of the playback in milliseconds, if available.
     */
    fun getCurrentPosition(): Int? {
        return if (prepared) player?.getCurrentPosition() else null
    }

    val applicationContext: Context
        get() = ref.getApplicationContext()

    val audioManager: AudioManager
        get() = ref.getAudioManager()

    /**
     * Playback handling methods
     */
    fun resume() {
        logger.blue("resume(): playing=$playing, released=$released, prepared=$prepared")
        if (!playing && !released) {
            playing = true
            if (prepared) {
                logger.blue("resume() -> start()")
                start()
            }
        }
    }

    private fun start() {
        player?.start()
    }

    // Try to get audio focus and then start.
    // private fun requestFocusAndStart() ...

    fun stop() {
        logger.log("stop()")
        focusManager.handleStop()
        if (released) {
            return
        }
        if (releaseMode != ReleaseMode.RELEASE) {
            pause()
            if (prepared) {
                player?.stop()
            }
        } else {
            release()
        }
    }

    fun release() {
        logger.log("release()")
        focusManager.handleStop()
        if (released) {
            return
        }
        if (playing) {
            player?.stop()
        }

        // Setting source to null will reset released, prepared and playing
        // and also calls player.release()
        source = null
    }

    fun pause() {
        logger.log("pause(): playing=$playing, released=$released, prepared=$prepared")
        if (!playing) return
        playing = false
        if (prepared) {
            player?.pause()
        }
    }

    // seek operations cannot be called until after
    // the player is ready.
    fun seek(position: Int) {
        shouldSeekTo = if (prepared) {
            player?.seekTo(position)
            -1
        } else {
            position
        }
    }

    /**
     * Player callbacks
     */
    fun onPrepared() {
        logger.log("onPrepared")
        prepared = true
        ref.handleDuration(this)
        if (playing) {
            start()
        }
        if (shouldSeekTo >= 0) {
            player?.seekTo(shouldSeekTo)
        }
    }

    fun onCompletion() {
        logger.log("onCompletion. releaseMode=$releaseMode")
        if (releaseMode != ReleaseMode.LOOP) {
            stop()
        }
        ref.handleComplete(this)
    }

    @Suppress("UNUSED_PARAMETER")
    fun onBuffering(percent: Int) {
        // TODO(luan): expose this as a stream
    }

    fun onSeekComplete() {
        ref.handleSeekComplete(this)
    }

    fun handleLog(message: String) {
        ref.handleLog(this, message)
    }

    fun handleError(errorCode: String?, errorMessage: String?, errorDetails: Any?) {
        ref.handleError(this, errorCode, errorMessage, errorDetails)
    }

    /**
     * Internal logic. Private methods
     */

    /**
     * Create new player
     */
    private fun createPlayer(): PlayerWrapper {
        return ExoPlayerWrapper(this, ref.getApplicationContext())
    }

    private fun PlayerWrapper.configAndPrepare() {
        logger.log("configAndPrepare()")
        setVolumeAndBalance(volume, balance)
        setLooping(isLooping)
        prepare()
    }

    private fun PlayerWrapper.setVolumeAndBalance(volume: Float, balance: Float) {
        val leftVolume = min(1f, 1f - balance) * volume
        val rightVolume = min(1f, 1f + balance) * volume
        setVolume(leftVolume, rightVolume)
    }

    fun dispose() {
        logger.log("dispose()")
        release()
        player?.dispose()
        player = null
        eventHandler.dispose()
    }
}

private val logger = Logger("WrappedPlayer: ")
