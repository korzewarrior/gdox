package org.korze.gdox.android

import android.app.Activity
import android.app.ActivityManager
import android.content.Intent
import android.graphics.Typeface
import android.hardware.usb.UsbManager
import android.net.Uri
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.Gravity
import android.view.View
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView

class GdoxActivity : Activity() {
  private lateinit var permission: GdoxUsbPermission
  private lateinit var discMonitor: GdoxDiscMonitor
  private lateinit var status: TextView
  private lateinit var detail: TextView
  private lateinit var primary: Button
  private lateinit var eject: Button
  private var waitingToStart = false
  private var emulatorLaunching = false
  private var autoStartConsumed = false
  private var driveLossPending = false
  private var redundantAttach = false
  private var restartRequested = false
  private var discTitle: String? = null
  private var started = false
  private var destroyed = false
  private val restartHandler = Handler(Looper.getMainLooper())
  private val emulatorExitCheck = object : Runnable {
    override fun run() {
      if (destroyed ||
        !started ||
        emulatorLaunching ||
        restartRequested) {
        return
      }
      if (emulatorProcessRunning()) {
        restartHandler.postDelayed(this, emulatorExitPollMs)
      } else {
        updateState()
      }
    }
  }

  private val preferences by lazy { GdoxCoreFiles.preferences(this) }

  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)
    synchronizeRecoveryState()
    val pendingRestart =
      intent?.getBooleanExtra(GDOX_RESTART_EXTRA, false) == true
    permission = GdoxUsbPermission(this) {
      runOnUiThread {
        updateState()
      }
    }
    discMonitor = GdoxDiscMonitor(this) {
      updateState()
    }
    val isUsbAttach = intent?.action == UsbManager.ACTION_USB_DEVICE_ATTACHED
    if (savedInstanceState == null &&
      isUsbAttach &&
      !pendingRestart &&
      (emulatorSessionStarting || emulatorProcessRunning())) {
      redundantAttach = true
      finish()
      return
    }
    setContentView(buildContent())
    primary.setOnClickListener { onPrimary() }
    acceptRestart(intent)
  }

  override fun onStart() {
    super.onStart()
    if (!redundantAttach) {
      started = true
      permission.start()
      updateState()
    }
  }

  override fun onResume() {
    super.onResume()
    if (redundantAttach) return
    synchronizeRecoveryState()
    if (emulatorLaunching) {
      emulatorLaunching = false
      emulatorSessionStarting = false
      autoStartConsumed = true
      discTitle = null
    }
    updateState()
  }

  override fun onStop() {
    if (!redundantAttach) {
      started = false
      restartHandler.removeCallbacks(emulatorExitCheck)
      discMonitor.stop()
      permission.stop()
    }
    super.onStop()
  }

  override fun onNewIntent(intent: Intent) {
    super.onNewIntent(intent)
    setIntent(intent)
    synchronizeRecoveryState()
    acceptRestart(intent)
    updateState()
  }

  override fun onDestroy() {
    destroyed = true
    restartHandler.removeCallbacksAndMessages(null)
    if (::discMonitor.isInitialized) discMonitor.release()
    super.onDestroy()
  }

  private fun buildContent(): View {
    val compact = GdoxUi.compactHeight(this)
    val content = GdoxUi.content(this)
    val header = LinearLayout(this).apply {
      orientation = LinearLayout.HORIZONTAL
      gravity = Gravity.CENTER_VERTICAL
    }
    header.addView(GdoxUi.heading(this, "GDOX", if (compact) 34f else 38f).apply {
      letterSpacing = 0.08f
    }, LinearLayout.LayoutParams(0, wrap, 1f))
    header.addView(textLink("gdox.korze.org") {
      startActivity(Intent(Intent.ACTION_VIEW, Uri.parse("https://gdox.korze.org")))
    })
    content.addView(header, LinearLayout.LayoutParams(match, wrap))
    content.addView(GdoxUi.sectionLabel(this, "Original Xbox"), margin(top = 2))

    val stateCard = LinearLayout(this).apply {
      orientation = LinearLayout.VERTICAL
      gravity = Gravity.CENTER_HORIZONTAL
      setPadding(
        GdoxUi.dp(this@GdoxActivity, 22),
        GdoxUi.dp(this@GdoxActivity, if (compact) 18 else 34),
        GdoxUi.dp(this@GdoxActivity, 22),
        GdoxUi.dp(this@GdoxActivity, if (compact) 16 else 30)
      )
      GdoxUi.card(this)
    }
    status = TextView(this).apply {
      textSize = 25f
      typeface = Typeface.create("sans-serif", Typeface.BOLD)
      setTextColor(GdoxUi.text)
      gravity = Gravity.CENTER
    }
    detail = GdoxUi.body(this, "").apply {
      gravity = Gravity.CENTER
    }
    stateCard.addView(status, LinearLayout.LayoutParams(match, wrap))
    stateCard.addView(detail, margin(top = if (compact) 6 else 10))
    content.addView(
      stateCard,
      margin(
        top = if (compact) 14 else 34,
        bottom = if (compact) 12 else 18
      )
    )

    primary = Button(this).apply {
      GdoxUi.primary(this)
    }
    content.addView(primary, LinearLayout.LayoutParams(match, wrap))

    val secondary = LinearLayout(this).apply {
      orientation = LinearLayout.HORIZONTAL
    }
    eject = Button(this).apply {
      text = "Eject"
      GdoxUi.secondary(this)
      setOnClickListener { discMonitor.eject() }
    }
    secondary.addView(eject, LinearLayout.LayoutParams(0, wrap, 1f))
    secondary.addView(Button(this).apply {
      text = "Sources"
      GdoxUi.secondary(this)
      setOnClickListener { openSources() }
    }, LinearLayout.LayoutParams(0, wrap, 1f).apply {
      marginStart = GdoxUi.dp(this@GdoxActivity, 8)
    })
    secondary.addView(Button(this).apply {
      text = "Settings"
      GdoxUi.secondary(this)
      setOnClickListener {
        startActivity(Intent(this@GdoxActivity, GdoxSettingsActivity::class.java))
      }
    }, LinearLayout.LayoutParams(0, wrap, 1f).apply {
      marginStart = GdoxUi.dp(this@GdoxActivity, 8)
    })
    content.addView(secondary, margin(top = if (compact) 8 else 10))

    content.addView(GdoxUi.body(
      this,
      "made by korze, with love, for gaming preservation everywhere"
    ).apply {
      gravity = Gravity.CENTER
      textSize = 12f
    }, margin(top = if (compact) 12 else 30))
    return GdoxUi.scrollable(this, content)
  }

  private fun textLink(label: String, action: () -> Unit) =
    TextView(this).apply {
      text = label
      textSize = 13f
      setTextColor(GdoxUi.muted)
      setPadding(GdoxUi.dp(this@GdoxActivity, 12), GdoxUi.dp(this@GdoxActivity, 10), 0, GdoxUi.dp(this@GdoxActivity, 10))
      setOnClickListener { action() }
    }

  private fun onPrimary() {
    val drive = permission.snapshot()
    when {
      !drive.connected -> refresh()
      !drive.permissionGranted -> {
        acknowledgeDriveLoss()
        waitingToStart = true
        permission.request()
      }
      !coreFilesConfigured() -> openSources()
      discMonitor.snapshot.state != GdoxDiscMonitor.State.READY ->
        updateState()
      else -> {
        acknowledgeDriveLoss()
        launchEmulator()
      }
    }
  }

  private fun canLaunch(): Boolean {
    val drive = permission.snapshot()
    return drive.connected &&
      drive.permissionGranted &&
      coreFilesConfigured() &&
      discMonitor.snapshot.deviceId == drive.deviceId &&
      discMonitor.snapshot.state == GdoxDiscMonitor.State.READY
  }

  private fun launchEmulator() {
    if (emulatorLaunching || !canLaunch()) return
    val deviceId = permission.snapshot().deviceId ?: return
    emulatorLaunching = true
    emulatorSessionStarting = true
    autoStartConsumed = true
    waitingToStart = false
    preferences.edit().remove("dvdPath").remove("dvdUri").apply()
    discTitle = null
    val activityName = packageManager.getApplicationInfo(
      packageName,
      android.content.pm.PackageManager.GET_META_DATA
    ).metaData?.getString(emulatorActivityMetadata)
      ?: error("Android emulator activity is not configured")
    refresh()
    discMonitor.handoff(deviceId) {
      val current = permission.snapshot()
      if (destroyed ||
        current.deviceId != deviceId ||
        !current.permissionGranted) {
        emulatorLaunching = false
        emulatorSessionStarting = false
        updateState()
      } else {
        startActivity(Intent().setClassName(this, activityName))
      }
    }
  }

  private fun emulatorProcessRunning(): Boolean {
    val manager = getSystemService(ACTIVITY_SERVICE) as ActivityManager
    val process = "$packageName:xemu"
    return manager.runningAppProcesses?.any { it.processName == process } == true
  }

  private fun refresh() {
    val drive = permission.snapshot()
    val media = discMonitor.snapshot

    when {
      !drive.connected -> {
        discTitle = null
        status.text =
          if (driveLossPending) "Drive disconnected" else "Connect the drive"
        detail.text = if (driveLossPending) {
          "Reconnect through a powered USB hub or adapter."
        } else {
          "Plug in the supported USB DVD drive."
        }
        primary.text = "Waiting for drive"
        primary.isEnabled = false
        eject.isEnabled = false
      }
      !drive.permissionGranted -> {
        status.text =
          if (driveLossPending) "Drive reconnected" else "Drive connected"
        detail.text = if (driveLossPending) {
          "The USB connection was interrupted. Allow access, then start again."
        } else {
          drive.productName ?: "USB DVD drive"
        }
        primary.text = "Allow drive access"
        primary.isEnabled = true
        eject.isEnabled = false
      }
      else -> {
        when (media.state) {
          GdoxDiscMonitor.State.EMPTY -> {
            if (!driveLossPending) autoStartConsumed = false
            discTitle = null
            status.text = "Insert an Xbox disc"
            detail.text = recoveryDetail(drive.productName)
          }
          GdoxDiscMonitor.State.READY -> {
            if (media.title != null && media.title != discTitle) {
              discTitle = media.title
            }
            if (discTitle != null) {
              status.text = discTitle
            } else {
              status.text = "Disc ready"
            }
            detail.text = recoveryDetail(drive.productName)
          }
          GdoxDiscMonitor.State.UNSUPPORTED -> {
            discTitle = null
            status.text = "Xbox 360 disc detected"
            detail.text = media.error
              ?: "Xbox 360 playback is unavailable on Android."
          }
          GdoxDiscMonitor.State.ERROR -> {
            status.text = "Drive needs attention"
            detail.text = "Reconnect the drive if it does not recover."
          }
          GdoxDiscMonitor.State.HANDOFF -> {
            status.text = "Starting game"
            detail.text = drive.productName ?: "Original Xbox disc drive"
          }
          else -> {
            status.text = "Checking drive"
            detail.text = recoveryDetail(drive.productName)
          }
        }
        when {
          !coreFilesConfigured() -> {
            primary.text = "Choose emulator files"
            primary.isEnabled = true
          }
          media.state == GdoxDiscMonitor.State.EMPTY -> {
            primary.text = "Waiting for disc"
            primary.isEnabled = false
          }
          media.state == GdoxDiscMonitor.State.READY -> {
            primary.text = "Start game"
            primary.isEnabled = !emulatorLaunching
          }
          media.state == GdoxDiscMonitor.State.UNSUPPORTED -> {
            primary.text = "Unavailable on Android"
            primary.isEnabled = false
          }
          else -> {
            primary.text =
              if (emulatorLaunching) "Starting game" else "Checking drive"
            primary.isEnabled = false
          }
        }
        eject.isEnabled = (
          media.state == GdoxDiscMonitor.State.READY ||
            media.state == GdoxDiscMonitor.State.UNSUPPORTED
          ) && !emulatorLaunching
      }
    }
  }

  private fun openSources() {
    startActivity(Intent(this, GdoxSourcesActivity::class.java))
  }

  private fun updateState() {
    val drive = permission.snapshot()
    val emulatorActive = emulatorProcessRunning()
    if (started &&
      !emulatorLaunching &&
      emulatorActive &&
      !restartRequested) {
      restartHandler.removeCallbacks(emulatorExitCheck)
      restartHandler.postDelayed(emulatorExitCheck, emulatorExitPollMs)
    } else {
      restartHandler.removeCallbacks(emulatorExitCheck)
    }
    if (started &&
      !emulatorLaunching &&
      !emulatorActive &&
      drive.permissionGranted &&
      drive.deviceId != null) {
      discMonitor.start(drive.deviceId)
    } else if (!emulatorLaunching &&
      (!started ||
        emulatorActive ||
        !drive.permissionGranted ||
        drive.deviceId == null)) {
      discMonitor.stop()
    }
    refresh()
    if (restartRequested) {
      scheduleRestart()
      return
    }
    val enabled = preferences.getBoolean(GdoxEmulatorPolicy.autoStartKey, true)
    if (enabled &&
      !driveLossPending &&
      !autoStartConsumed &&
      canLaunch()) {
      launchEmulator()
      return
    }
    if (waitingToStart && canLaunch()) {
      launchEmulator()
    }
  }

  private fun acceptRestart(intent: Intent?) {
    if (intent?.getBooleanExtra(GDOX_RESTART_EXTRA, false) == true) {
      intent.removeExtra(GDOX_RESTART_EXTRA)
      restartRequested = true
      autoStartConsumed = true
      scheduleRestart()
    }
  }

  private fun synchronizeRecoveryState() {
    if (!GdoxSessionRecovery.driveLossPending(this)) return
    driveLossPending = true
    autoStartConsumed = true
  }

  private fun acknowledgeDriveLoss() {
    if (!driveLossPending) return
    if (GdoxSessionRecovery.acknowledgeDriveLoss(this)) {
      driveLossPending = false
    }
  }

  private fun recoveryDetail(productName: String?): String =
    if (driveLossPending) {
      "Drive reconnected. Use a powered hub if it disconnects again."
    } else {
      productName ?: "Original Xbox disc drive"
    }

  private fun scheduleRestart() {
    restartHandler.removeCallbacksAndMessages(null)
    restartHandler.postDelayed({
      if (!restartRequested) return@postDelayed
      if (emulatorProcessRunning()) {
        scheduleRestart()
      } else {
        restartRequested = false
        autoStartConsumed = false
        updateState()
      }
    }, 300)
  }

  private fun coreFilesConfigured(): Boolean = GdoxCoreFiles.configured(this)

  private fun margin(top: Int = 0, bottom: Int = 0) =
    LinearLayout.LayoutParams(match, wrap).apply {
      topMargin = GdoxUi.dp(this@GdoxActivity, top)
      bottomMargin = GdoxUi.dp(this@GdoxActivity, bottom)
    }

  private companion object {
    const val match = LinearLayout.LayoutParams.MATCH_PARENT
    const val wrap = LinearLayout.LayoutParams.WRAP_CONTENT
    const val emulatorActivityMetadata =
      "org.korze.gdox.EMULATOR_ACTIVITY"
    const val emulatorExitPollMs = 300L

    @Volatile
    var emulatorSessionStarting = false
  }
}
