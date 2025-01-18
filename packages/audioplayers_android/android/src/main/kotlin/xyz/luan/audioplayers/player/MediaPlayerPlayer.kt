package xyz.luan.audioplayers.player

import android.media.MediaPlayer
import android.media.audiofx.Equalizer
import android.os.Build
import android.os.PowerManager
import xyz.luan.audioplayers.AudioContextAndroid
import xyz.luan.audioplayers.source.Source

class MediaPlayerPlayer: Player {
    private val wrappedPlayer: WrappedPlayer
    private val mediaPlayer: MediaPlayer
    private var equalizer: Equalizer

    public constructor(wrappedPlayer: WrappedPlayer) {
        this.wrappedPlayer = wrappedPlayer
        this.mediaPlayer = createMediaPlayer(wrappedPlayer)

        val audioSessionId = this.mediaPlayer.getAudioSessionId()
        this.equalizer = Equalizer(0, audioSessionId)
    }

    private fun createMediaPlayer(wrappedPlayer: WrappedPlayer): MediaPlayer {
        val mediaPlayer = MediaPlayer().apply {
            setOnPreparedListener { wrappedPlayer.onPrepared() }
            setOnCompletionListener { wrappedPlayer.onCompletion() }
            setOnSeekCompleteListener { wrappedPlayer.onSeekComplete() }
            setOnErrorListener { _, what, extra -> wrappedPlayer.onError(what, extra) }
            setOnBufferingUpdateListener { _, percent -> wrappedPlayer.onBuffering(percent) }
        }
        wrappedPlayer.context.setAttributesOnPlayer(mediaPlayer)
        return mediaPlayer
    }

    override fun getDuration(): Int? {
        // media player returns -1 if the duration is unknown
        return mediaPlayer.duration.takeUnless { it == -1 }
    }

    override fun getCurrentPosition(): Int {
        return mediaPlayer.currentPosition
    }

    /**
     * Equalizer methods
     */
    override fun getEqEnabled(): Boolean {
        return this.equalizer.getEnabled()
    }

    override fun setEqEnabled(isEnabled: Boolean) {
        this.equalizer.setEnabled(isEnabled)
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

    override fun setVolume(leftVolume: Float, rightVolume: Float) {
        mediaPlayer.setVolume(leftVolume, rightVolume)
    }
    /**
     * End Equalizer methods
     */

    override fun setRate(rate: Float) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            mediaPlayer.playbackParams = mediaPlayer.playbackParams.setSpeed(rate)
        } else if (rate == 1.0f) {
            mediaPlayer.start()
        } else {
            error("Changing the playback rate is only available for Android M/23+ or using LOW_LATENCY mode.")
        }
    }

    override fun setSource(source: Source) {
        reset()
        source.setForMediaPlayer(mediaPlayer)
    }

    override fun setLooping(looping: Boolean) {
        mediaPlayer.isLooping = looping
    }

    override fun start() {
        // Setting playback rate instead of mediaPlayer.start().
        setRate(wrappedPlayer.rate)
    }

    override fun pause() {
        mediaPlayer.pause()
    }

    override fun stop() {
        mediaPlayer.stop()
    }

    override fun release() {
        mediaPlayer.reset()
        mediaPlayer.release()
    }

    override fun seekTo(position: Int) {
        mediaPlayer.seekTo(position)
    }

    override fun updateContext(context: AudioContextAndroid) {
        context.setAttributesOnPlayer(mediaPlayer)
        if (context.stayAwake) {
            mediaPlayer.setWakeMode(wrappedPlayer.applicationContext, PowerManager.PARTIAL_WAKE_LOCK)
        }
    }

    override fun prepare() {
        mediaPlayer.prepareAsync()
    }

    override fun reset() {
        mediaPlayer.reset()
    }

    override fun isLiveStream(): Boolean {
        val duration = getDuration()
        return duration == null || duration == 0
    }
}
