package xyz.luan.audioplayers.source

import xyz.luan.audioplayers.Logger
import android.net.Uri
import androidx.media3.common.C
import androidx.media3.datasource.DataSource
import androidx.media3.datasource.DataSpec
import androidx.media3.datasource.TransferListener
import java.io.IOException
import java.util.concurrent.BlockingQueue
import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.TimeUnit
import kotlin.jvm.Synchronized

class ByteStreamSource() : DataSource, Source {
    lateinit var buffersQueue: BlockingQueue<ByteArray>
    private var currentBuffer: ByteArray? = null
    private var bufferPos = 0
    private var opened = false

    private val transferListeners = mutableListOf<TransferListener>()
    private var openedUri: Uri? = null

    override fun open(dataSpec: DataSpec): Long {
        logger.blue("open(). uri=${dataSpec.uri}")
        opened = true
        openedUri = dataSpec.uri
        var len = C.LENGTH_UNSET.toLong()
        logger.blue("len: ${len}")
        return len
    }

    @Synchronized
    @Throws(IOException::class)
    override fun read(target: ByteArray, offset: Int, length: Int): Int {
        // logger.log("Read")
        if (!opened) throw IOException("Not opened")
        while (currentBuffer == null || bufferPos >= currentBuffer!!.size) {
            var next: ByteArray
            try {
                logger.warn("----- Take Buffer -----")
                next = buffersQueue.take()
                logger.log("size=${next.size}")
            } catch (e: InterruptedException) {
                logger.error( "InterruptedException. Sending end of input")
                return C.RESULT_END_OF_INPUT
            } catch (e: Exception) {
                logger.error( "Exception. Sending end of input")
                return C.RESULT_END_OF_INPUT
            }
            if (next.isEmpty()) return C.RESULT_END_OF_INPUT
            currentBuffer = next
            bufferPos = 0
        }
        val avail = currentBuffer!!.size - bufferPos
        val toCopy = minOf(avail, length)
        System.arraycopy(currentBuffer!!, bufferPos, target, offset, toCopy)
        bufferPos += toCopy
        return toCopy
    }

    override fun getUri(): Uri? = openedUri

    override fun close() {
        logger.blue("close()")
        opened = false
        currentBuffer = null
        bufferPos = 0
        buffersQueue.clear()
    }

    // Required by DataSource interface:
    override fun addTransferListener(transferListener: TransferListener) {
        logger.blue("addTransferListener")
        synchronized(transferListeners) { transferListeners.add(transferListener) }
    }
}

private val logger = Logger("ByteStreamSource: ")
