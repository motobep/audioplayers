package xyz.luan.audioplayers.player

import xyz.luan.audioplayers.AudioContextAndroid
import xyz.luan.audioplayers.source.Source

interface Player {
    fun getDuration(): Int?
    fun getCurrentPosition(): Int?
    fun isActuallyPlaying(): Boolean
    fun isLiveStream(): Boolean

    fun start()
    fun pause()
    fun stop()
    fun seekTo(position: Int)
    fun release()

    fun getEqEnabled(): Boolean
    fun setEqEnabled(isEnabled: Boolean)
    fun getEqNumberOfBands(): Short
    fun getEqLimits(): Map<String, List<Float>>
    fun getEqBand(bandIndex: Short): Map<String, Float>
    fun setEqBand(bandIndex: Short, band: Map<String, Float>)

    fun setVolume(leftVolume: Float, rightVolume: Float)
    fun setRate(rate: Float)
    fun setLooping(looping: Boolean)
    fun updateContext(context: AudioContextAndroid)
    fun setSource(source: Source)

    fun prepare()
    fun reset()
}
