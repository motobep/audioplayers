package xyz.luan.audioplayers.player

import xyz.luan.audioplayers.Logger
import android.content.Context
import android.net.Uri
import android.os.Build
import android.util.SparseArray
import androidx.annotation.RequiresApi
import androidx.media3.common.AudioAttributes
import androidx.media3.common.C
import androidx.media3.common.C.TIME_UNSET
import androidx.media3.common.MediaItem
import androidx.media3.common.PlaybackException
import androidx.media3.common.Player
import androidx.media3.common.audio.AudioProcessor
import androidx.media3.common.audio.AudioProcessor.UnhandledAudioFormatException
import androidx.media3.common.audio.BaseAudioProcessor
import androidx.media3.common.audio.ChannelMixingMatrix
import androidx.media3.datasource.ByteArrayDataSource
import androidx.media3.datasource.DataSource
import androidx.media3.exoplayer.DefaultRenderersFactory
import androidx.media3.exoplayer.ExoPlayer
import androidx.media3.exoplayer.audio.AudioSink
import androidx.media3.exoplayer.audio.DefaultAudioSink
import androidx.media3.exoplayer.source.MediaSource
import androidx.media3.exoplayer.source.ProgressiveMediaSource
import xyz.luan.audioplayers.AudioContextAndroid
import xyz.luan.audioplayers.source.ByteStreamSource
import xyz.luan.audioplayers.source.BytesSource
import xyz.luan.audioplayers.source.Source
import xyz.luan.audioplayers.source.UrlSource
import java.nio.ByteBuffer
import java.util.concurrent.LinkedBlockingQueue
import android.media.audiofx.Equalizer

class ExoPlayerWrapper(
    private val wrappedPlayer: WrappedPlayer,
    appContext: Context,
) : PlayerWrapper {

    class ExoPlayerListener(private val wrappedPlayer: WrappedPlayer) : androidx.media3.common.Player.Listener {
        override fun onPlayerError(error: PlaybackException) {
            logger.error("onPlayerError: $error")
            if (error.errorCode == PlaybackException.ERROR_CODE_PARSING_CONTAINER_UNSUPPORTED ||
                error.errorCode == PlaybackException.ERROR_CODE_IO_FILE_NOT_FOUND
            ) {
                wrappedPlayer.handleError(
                    errorCode = "AndroidAudioError",
                    errorMessage = "Failed to set source. For troubleshooting, see: " +
                            "https://github.com/bluefireteam/audioplayers/blob/main/troubleshooting.md",
                    errorDetails = "${error.errorCodeName}\n${error.message}\n${error.stackTraceToString()}",
                )
                return
            }
            wrappedPlayer.handleError(
                errorCode = error.errorCodeName,
                errorMessage = error.message,
                errorDetails = error.stackTraceToString(),
            )
        }

        override fun onPlaybackStateChanged(playbackState: Int) {
            logger.log("onPlaybackStateChanged: $playbackState")
            when (playbackState) {
                Player.STATE_IDLE -> {} // TODO(gustl22): may can use or leave as no-op
                Player.STATE_BUFFERING -> wrappedPlayer.onBuffering(0)
                Player.STATE_READY -> wrappedPlayer.onPrepared()
                Player.STATE_ENDED -> wrappedPlayer.onCompletion()
            }
        }
    }

    private var player: ExoPlayer
    private lateinit var equalizer: Equalizer

    @androidx.annotation.OptIn(androidx.media3.common.util.UnstableApi::class)
    private var channelMixingAudioProcessor = AdaptiveChannelMixingAudioProcessor()
    private lateinit var audioSink: AudioSink

    private val buffersQueue = LinkedBlockingQueue<ByteArray>(50)

    init {
        logger.log("init")
        player = createPlayer(appContext)

        logger.log("getAudioSessionId()")
        val audioSessionId = player.getAudioSessionId()
        if (audioSessionId != 0) {
            logger.blue("try")
            try {
                logger.log("audioSessionId: ${audioSessionId}")
                equalizer = Equalizer(0, audioSessionId)
                equalizer?.enabled = true
            } catch (e: Exception) {
                logger.error("e: $e")
                e.printStackTrace()
            }
        } else {
            player.addListener(object : Player.Listener {
                override fun onAudioSessionIdChanged(sessionId: Int) {
                    logger.blue("onAudioSessionIdChanged: $sessionId")
                    if (sessionId != 0) {
                        logger.blue("make equalizer")
                        equalizer = Equalizer(0, sessionId)
                        equalizer?.enabled = true
                    }
                }
            })
        }
    }

    @androidx.annotation.OptIn(androidx.media3.common.util.UnstableApi::class)
    private fun createPlayer(appContext: Context): ExoPlayer {
        val renderersFactory = object : DefaultRenderersFactory(appContext) {
            override fun buildAudioSink(
                context: Context,
                enableFloatOutput: Boolean,
                enableAudioTrackPlaybackParams: Boolean,
            ): AudioSink {
                audioSink =
                    DefaultAudioSink.Builder(appContext).setAudioProcessors(arrayOf(channelMixingAudioProcessor))
                        .build()
                return audioSink
            }
        }

        return ExoPlayer.Builder(appContext).setRenderersFactory(renderersFactory).build().apply {
            addListener(ExoPlayerListener(wrappedPlayer))
        }
    }

    override fun getDuration(): Int? {
        if (player.isCurrentMediaItemLive) {
            return null
        }
        return (player.duration.takeUnless { it == TIME_UNSET })?.toInt()
    }

    override fun getCurrentPosition(): Int {
        return player.currentPosition.toInt()
    }

    /**
     * Equalizer methods
     */
    override fun getEqEnabled(): Boolean {
        return equalizer.getEnabled()
    }

    override fun setEqEnabled(isEnabled: Boolean) {
        equalizer.setEnabled(isEnabled)
    }

    override fun getEqNumberOfBands(): Short {
        return equalizer.getNumberOfBands()
    }

    override fun getEqLimits(): Map<String, List<Float>> {
        val range = equalizer.getBandLevelRange().map { it / 100.0f } // milli -> dB

        return mapOf(
            "gain" to range,
            "bandwidth" to listOf(),
            "frequency" to listOf(),
        )
    }

    override fun getEqBand(bandIndex: Short): Map<String, Float> {
        val gainMilliB = equalizer.getBandLevel(bandIndex) // milli Bel
        val gain: Float = gainMilliB / 100.0f // dB

        val freqRange = equalizer.getBandFreqRange(bandIndex)
        val bandwidth: Float = (freqRange[1] - freqRange[0]) / 1000.0f // milli -> Hz

        val freq: Float = equalizer.getCenterFreq(bandIndex) / 1000.0f // milli -> Hz

        return mapOf("gain" to gain, "bandwidth" to bandwidth, "frequency" to freq)
    }

    override fun setEqBand(bandIndex: Short, band: Map<String, Float>) {
        val gainMilliB: Short = (band["gain"]!! * 100).toInt().toShort()
        equalizer.setBandLevel(bandIndex, gainMilliB)
    }

    /**
     * End Equalizer methods
     */

    override fun start() {
        logger.log("start()")
        player.play()
    }

    override fun pause() {
        logger.log("pause()")
        player.pause()
    }

    override fun stop() {
        logger.log("stop()")
        player.pause()
        player.seekTo(0)
    }

    override fun seekTo(position: Int) {
        player.seekTo(position.toLong())
        wrappedPlayer.onSeekComplete()
    }

    override fun release() {
        logger.log("release()")
        player.stop()
        player.clearMediaItems()
    }

    override fun dispose() {
        logger.log("dispose()")
        release()
        player.release()
    }

    @androidx.annotation.OptIn(androidx.media3.common.util.UnstableApi::class)
    override fun setVolume(leftVolume: Float, rightVolume: Float) {
        this.channelMixingAudioProcessor.putChannelMixingMatrix(
            ChannelMixingMatrix(2, 2, floatArrayOf(leftVolume, 0f, 0f, rightVolume)),
        )
    }

    override fun setRate(rate: Float) {
        player.setPlaybackSpeed(rate)
    }

    override fun setLooping(looping: Boolean) {
        player.repeatMode = if (looping) {
            Player.REPEAT_MODE_ONE
        } else {
            Player.REPEAT_MODE_OFF
        }
    }

    override fun updateContext(context: AudioContextAndroid) {
        val builder = AudioAttributes.Builder()
        builder.setContentType(context.contentType)
        builder.setUsage(context.usageType)

        player.setAudioAttributes(
            builder.build(),
            false,
        )
    }

    @RequiresApi(Build.VERSION_CODES.M)
    @androidx.annotation.OptIn(androidx.media3.common.util.UnstableApi::class)
    override fun setSource(source: Source) {
        logger.blue("Exo player: setSource($source)")
        player.clearMediaItems()
        if (source is UrlSource) {
            player.setMediaItem(MediaItem.fromUri(source.url))
        } else if (source is BytesSource) {
            val byteArrayDataSource = ByteArrayDataSource(source.data)
            val factory = DataSource.Factory { byteArrayDataSource; }
            val mediaSource: MediaSource = ProgressiveMediaSource.Factory(factory).createMediaSource(
                MediaItem.fromUri(Uri.EMPTY),
            )
            player.setMediaSource(mediaSource)
        } else if (source is ByteStreamSource) {
            source.buffersQueue = buffersQueue
            val factory = DataSource.Factory { source as DataSource }
            val mediaSource = ProgressiveMediaSource.Factory(factory)
                .createMediaSource(MediaItem.fromUri("stream://local"))
            player.setMediaSource(mediaSource)
            // TODO: signal on byteStreamSource prepared (or just prepared?)
            logger.warn("ExoPlayer (ByteStreamSource): signaling onPrepared")
            wrappedPlayer.onPrepared()
        }
    }

    override fun prepare() {
        player.prepare()
    }

    override fun pushBuffer(buffer: ByteArray) {
        logger.log("pushBuffer")
        buffersQueue.put(buffer)
    }

    override fun flushBuffers() {
        logger.log("flushBuffers")
        buffersQueue.clear()
        player!!.seekTo(0)
    }
}

/**
 * See Implementation of [androidx.media3.common.audio.ChannelMixingAudioProcessor] for reference.
 * See: https://github.com/androidx/media/blob/8ea49025aaf14c7e7d953df8ca2f08a76d9d4275/libraries/common/src/main/java/androidx/media3/common/audio/ChannelMixingAudioProcessor.java
 */
@androidx.annotation.OptIn(androidx.media3.common.util.UnstableApi::class)
class AdaptiveChannelMixingAudioProcessor : BaseAudioProcessor() {
    private val matrixByInputChannelCount: SparseArray<ChannelMixingMatrix?> = SparseArray<ChannelMixingMatrix?>()

    fun putChannelMixingMatrix(matrix: ChannelMixingMatrix) {
        matrixByInputChannelCount.put(matrix.inputChannelCount, matrix)
    }

    @Throws(UnhandledAudioFormatException::class)
    override fun onConfigure(inputAudioFormat: AudioProcessor.AudioFormat): AudioProcessor.AudioFormat {
        if (inputAudioFormat.encoding != C.ENCODING_PCM_16BIT) {
            throw UnhandledAudioFormatException(inputAudioFormat)
        } else {
            // We keep the same format; we're not altering the channel count.
            return inputAudioFormat
        }
    }

    override fun queueInput(inputBuffer: ByteBuffer) {
        val channelMixingMatrix = matrixByInputChannelCount[inputAudioFormat.channelCount]
        if (channelMixingMatrix == null || channelMixingMatrix.isIdentity) {
            // No need to transform, if balance is equalized.
            val outputBuffer = this.replaceOutputBuffer(inputBuffer.remaining())
            if (inputBuffer.hasRemaining()) {
                outputBuffer.put(inputBuffer)
            }
            outputBuffer.flip()
            return
        }

        val outputBuffer = this.replaceOutputBuffer(inputBuffer.remaining())
        val inputChannelCount = channelMixingMatrix.inputChannelCount
        val outputChannelCount = channelMixingMatrix.outputChannelCount
        val outputFrame = FloatArray(outputChannelCount)

        while (inputBuffer.hasRemaining()) {
            var inputValue: Short
            var inputChannelIndex = 0
            while (inputChannelIndex < inputChannelCount) {
                inputValue = inputBuffer.getShort()

                for (outputChannelIndex in 0 until outputChannelCount) {
                    outputFrame[outputChannelIndex] += channelMixingMatrix.getMixingCoefficient(
                        inputChannelIndex,
                        outputChannelIndex,
                    ) * inputValue.toFloat()
                }
                ++inputChannelIndex
            }

            inputChannelIndex = 0
            while (inputChannelIndex < outputChannelCount) {
                inputValue =
                    outputFrame[inputChannelIndex].toInt().coerceIn(-32768, 32767).toShort()
                outputBuffer.put((inputValue.toInt() and 255).toByte())
                outputBuffer.put((inputValue.toInt() shr 8 and 255).toByte())
                outputFrame[inputChannelIndex] = 0.0f
                ++inputChannelIndex
            }
        }
        outputBuffer.flip()
    }
}

private val logger = Logger("ExoPlayerWrapper: ")
