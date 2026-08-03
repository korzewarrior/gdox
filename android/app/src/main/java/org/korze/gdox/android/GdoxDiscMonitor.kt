package org.korze.gdox.android

import android.content.Context
import android.os.Handler
import android.os.Looper
import android.util.Log
import java.io.IOException
import java.util.concurrent.ScheduledThreadPoolExecutor
import java.util.concurrent.TimeUnit

internal class GdoxDiscMonitor(
  context: Context,
  private val onChanged: () -> Unit
) {
  enum class State {
    IDLE,
    CHECKING,
    EMPTY,
    READY,
    UNSUPPORTED,
    CHANGING,
    ERROR,
    HANDOFF
  }

  data class Snapshot(
    val deviceId: Int?,
    val state: State,
    val error: String?,
    val title: String? = null
  )

  private data class Identification(
    val platform: Int,
    val title: String?
  )

  private val main = Handler(Looper.getMainLooper())
  private val host = GdoxUsbHost(context.applicationContext)
  private val worker = ScheduledThreadPoolExecutor(1) { task ->
    Thread(task, "gdox-disc-monitor").apply { isDaemon = true }
  }.apply {
    removeOnCancelPolicy = true
  }
  @Volatile
  private var generation = 0L
  @Volatile
  private var desiredDeviceId: Int? = null
  @Volatile
  private var released = false
  private var nativeHandle = 0L
  private var failures = 0
  private var candidate: State? = null
  private var candidateObservations = 0
  private var titleProbeComplete = false
  private var unsupportedMedia = false

  var snapshot = Snapshot(null, State.IDLE, null)
    private set

  fun start(deviceId: Int) {
    if (released || desiredDeviceId == deviceId) return
    desiredDeviceId = deviceId
    val token = ++generation
    snapshot = Snapshot(deviceId, State.CHECKING, null)
    worker.execute {
      closeOnWorker()
      failures = 0
      candidate = null
      candidateObservations = 0
      titleProbeComplete = false
      unsupportedMedia = false
      schedule(token, deviceId, initialSettleMs)
    }
  }

  fun stop() {
    if (released || desiredDeviceId == null && snapshot.state == State.IDLE) {
      return
    }
    desiredDeviceId = null
    ++generation
    snapshot = Snapshot(null, State.IDLE, null)
    worker.execute { closeOnWorker() }
  }

  fun handoff(deviceId: Int, ready: () -> Unit) {
    if (released) return
    desiredDeviceId = null
    val token = ++generation
    snapshot = Snapshot(deviceId, State.HANDOFF, null)
    onChanged()
    worker.execute {
      val handedOff = closeOnWorker(handoff = true)
      main.post {
        if (!released && generation == token) {
          if (handedOff) {
            ready()
          } else {
            start(deviceId)
            onChanged()
          }
        }
      }
    }
  }

  fun eject() {
    val deviceId = desiredDeviceId ?: return
    val token = generation
    if (snapshot.state != State.READY && snapshot.state != State.UNSUPPORTED) {
      return
    }
    snapshot = Snapshot(deviceId, State.CHANGING, null)
    onChanged()
    worker.execute {
      try {
        if (isCurrent(token, deviceId) && nativeHandle != 0L) {
          ejectNative(nativeHandle)
          candidate = null
          candidateObservations = 0
        }
      } catch (error: IOException) {
        Log.w(logTag, "disc eject failed", error)
        publish(
          token,
          Snapshot(deviceId, State.ERROR, error.message)
        )
      }
    }
  }

  fun release() {
    if (released) return
    released = true
    desiredDeviceId = null
    ++generation
    snapshot = Snapshot(null, State.IDLE, null)
    worker.execute {
      closeOnWorker()
      worker.shutdown()
    }
  }

  private fun observe(token: Long, deviceId: Int) {
    if (!isCurrent(token, deviceId)) return
    try {
      if (nativeHandle == 0L) {
        val descriptor = host.openFileDescriptor()
        if (descriptor < 0) throw IOException("USB drive is not available.")
        nativeHandle = openNative(descriptor)
        if (nativeHandle == 0L) throw IOException("USB monitor did not open.")
      }
      val observed = when (pollNative(nativeHandle)) {
        mediaEmpty -> State.EMPTY
        mediaReady -> State.READY
        else -> State.CHANGING
      }
      failures = 0
      val stable = stabilize(token, deviceId, observed)
      if (observed == State.READY &&
        stable &&
        !titleProbeComplete) {
        titleProbeComplete = true
        publish(token, Snapshot(deviceId, State.CHANGING, null))
        val identification = identifyOnWorker()
        if (isCurrent(token, deviceId)) {
          unsupportedMedia = identification?.platform == mediaXbox360
          publish(
            token,
            if (unsupportedMedia) {
              Snapshot(
                deviceId,
                State.UNSUPPORTED,
                "Xbox 360 playback is unavailable on Android."
              )
            } else {
              Snapshot(deviceId, State.READY, null, identification?.title)
            }
          )
        }
      } else if (observed == State.EMPTY) {
        titleProbeComplete = false
        unsupportedMedia = false
      }
      schedule(
        token,
        deviceId,
        if (observed == State.CHANGING) changingPollMs else steadyPollMs
      )
    } catch (error: IOException) {
      Log.w(logTag, "drive observation failed", error)
      closeOnWorker()
      ++failures
      candidate = null
      candidateObservations = 0
      publish(
        token,
        Snapshot(
          deviceId,
          if (failures >= visibleFailureCount) State.ERROR else State.CHECKING,
          error.message
        )
      )
      schedule(
        token,
        deviceId,
        retryDelayMs()
      )
    }
  }

  private fun stabilize(
    token: Long,
    deviceId: Int,
    observed: State
  ): Boolean {
    if (observed == State.CHANGING) {
      candidate = null
      candidateObservations = 0
      publish(token, Snapshot(deviceId, State.CHANGING, null))
      return false
    }
    if (candidate == observed) {
      ++candidateObservations
    } else {
      candidate = observed
      candidateObservations = 1
    }
    val stable = candidateObservations >= stableObservationCount
    if (observed != State.READY || titleProbeComplete || !stable) {
      val publishedState = when {
        !stable -> State.CHANGING
        observed == State.READY && unsupportedMedia -> State.UNSUPPORTED
        else -> observed
      }
      publish(
        token,
        Snapshot(
          deviceId,
          publishedState,
          if (publishedState == State.UNSUPPORTED) {
            "Xbox 360 playback is unavailable on Android."
          } else {
            null
          }
        )
      )
    }
    return stable
  }

  private fun schedule(token: Long, deviceId: Int, delayMs: Long) {
    if (!isCurrent(token, deviceId)) return
    worker.schedule(
      { observe(token, deviceId) },
      delayMs,
      TimeUnit.MILLISECONDS
    )
  }

  private fun publish(token: Long, value: Snapshot) {
    main.post {
      if (!released && generation == token) {
        snapshot = value
        onChanged()
      }
    }
  }

  private fun retryDelayMs(): Long {
    val shift = (failures - 1).coerceIn(0, retryMaxShift)
    return (retryBaseMs shl shift).coerceAtMost(retryMaxMs)
  }

  private fun identifyOnWorker(): Identification? {
    if (!closeOnWorker(handoff = true)) return null
    val descriptor = host.openFileDescriptor()
    if (descriptor < 0) return null
    val identification = try {
      val payload = identifyNative(descriptor)
      if (payload.isEmpty()) {
        null
      } else {
        Identification(
          payload[0].toInt() and 0xff,
          payload.copyOfRange(1, payload.size)
            .toString(Charsets.UTF_8)
            .trim()
            .takeIf(String::isNotEmpty)
        )
      }
    } catch (error: IOException) {
      Log.w(logTag, "disc title identification failed", error)
      null
    }
    try {
      nativeHandle = openNative(descriptor)
      if (nativeHandle == 0L) {
        throw IOException("USB monitor did not reopen.")
      }
    } catch (error: IOException) {
      Log.w(logTag, "drive monitor reopen failed", error)
      nativeHandle = 0L
      host.close()
    }
    return identification
  }

  private fun closeOnWorker(handoff: Boolean = false): Boolean {
    val handle = nativeHandle
    var closed = !handoff
    if (handle != 0L) {
      try {
        if (handoff) {
          handoffNative(handle)
        } else {
          closeNative(handle)
        }
        closed = true
      } catch (error: IOException) {
        Log.w(logTag, "drive monitor close failed", error)
      } finally {
        nativeHandle = 0L
      }
    }
    host.close()
    return closed
  }

  private fun isCurrent(token: Long, deviceId: Int): Boolean =
    !released && generation == token && desiredDeviceId == deviceId

  private external fun openNative(fileDescriptor: Int): Long
  private external fun pollNative(handle: Long): Int
  private external fun identifyNative(fileDescriptor: Int): ByteArray
  private external fun ejectNative(handle: Long)
  private external fun closeNative(handle: Long)
  private external fun handoffNative(handle: Long)

  private companion object {
    const val mediaEmpty = 0
    const val mediaReady = 1
    const val mediaXbox360 = 2
    const val stableObservationCount = 2
    const val visibleFailureCount = 3
    const val changingPollMs = 300L
    const val steadyPollMs = 850L
    const val initialSettleMs = 750L
    const val retryBaseMs = 750L
    const val retryMaxMs = 12000L
    const val retryMaxShift = 4
    const val logTag = "GDOX-disc"

    init {
      System.loadLibrary("gdox_disc_monitor")
    }
  }
}
