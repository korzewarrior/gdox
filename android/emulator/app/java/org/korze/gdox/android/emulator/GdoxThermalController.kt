package org.korze.gdox.android.emulator

import android.app.Activity
import android.os.Build
import android.os.PowerManager
import android.util.Log
import android.view.WindowManager
import org.korze.gdox.android.GdoxCoreFiles
import org.korze.gdox.android.GdoxEmulatorPolicy

internal interface GdoxThermalControl {
  fun start()
  fun stop()
  fun refresh()
}

internal fun createGdoxThermalController(
  activity: Activity
): GdoxThermalControl =
  if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
    GdoxThermalController(activity)
  } else {
    NoThermalControl
  }

private object NoThermalControl : GdoxThermalControl {
  override fun start() = Unit
  override fun stop() = Unit
  override fun refresh() = Unit
}

private class GdoxThermalController(
  private val activity: Activity
) : GdoxThermalControl {
  private val powerManager by lazy {
    activity.getSystemService(PowerManager::class.java)
  }
  private var listening = false
  private val listener =
    PowerManager.OnThermalStatusChangedListener(::applyThermalStatus)

  override fun start() {
    if (listening) return
    listening = true
    powerManager?.addThermalStatusListener(
      activity.mainExecutor,
      listener
    )
  }

  override fun stop() {
    if (!listening) return
    powerManager?.removeThermalStatusListener(listener)
    listening = false
    setBrightness(WindowManager.LayoutParams.BRIGHTNESS_OVERRIDE_NONE)
  }

  override fun refresh() {
    applyThermalStatus(
      powerManager?.currentThermalStatus
        ?: PowerManager.THERMAL_STATUS_NONE
    )
  }

  private fun applyThermalStatus(status: Int) {
    if (!listening) return
    val enabled = GdoxCoreFiles.preferences(activity).getBoolean(
      GdoxEmulatorPolicy.thermalManagementKey,
      true
    )
    val brightness =
      if (!enabled) {
        WindowManager.LayoutParams.BRIGHTNESS_OVERRIDE_NONE
      } else {
        when {
          status >= PowerManager.THERMAL_STATUS_MODERATE -> hotBrightness
          status >= PowerManager.THERMAL_STATUS_LIGHT -> warmBrightness
          else -> WindowManager.LayoutParams.BRIGHTNESS_OVERRIDE_NONE
        }
      }
    setBrightness(brightness)
    Log.i(
      logTag,
      "thermal status=$status brightness=${if (brightness < 0f) "system" else brightness}"
    )
  }

  private fun setBrightness(value: Float) {
    activity.runOnUiThread {
      activity.window.attributes = activity.window.attributes.apply {
        screenBrightness = value
      }
    }
  }

  private companion object {
    const val warmBrightness = 0.55f
    const val hotBrightness = 0.40f
    const val logTag = "GDOX-performance"
  }
}
