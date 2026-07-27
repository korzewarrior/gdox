package org.korze.gdox.android.emulator

import android.app.GameManager
import android.app.GameState
import android.content.Context
import android.content.Intent
import android.graphics.Color
import android.graphics.Typeface
import android.hardware.input.InputManager
import android.os.Build
import android.os.Bundle
import android.os.PowerManager
import android.util.Log
import android.view.Gravity
import android.view.InputDevice
import android.view.KeyEvent
import android.view.Surface
import android.view.View
import android.view.ViewGroup
import android.widget.RelativeLayout
import android.widget.TextView
import android.window.OnBackInvokedCallback
import android.window.OnBackInvokedDispatcher
import androidx.core.view.ViewCompat
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import org.korze.gdox.android.GdoxEmulatorPolicy
import org.korze.gdox.android.GdoxActivity
import org.korze.gdox.android.GdoxCoreFiles
import org.korze.gdox.android.GdoxSessionRecovery
import org.korze.gdox.android.GdoxUsbHost
import org.korze.gdox.android.GDOX_RESTART_EXTRA
import org.libsdl.app.SDLActivity
import org.libsdl.app.SDLControllerManager
import java.util.concurrent.ScheduledThreadPoolExecutor
import java.util.concurrent.TimeUnit

class GdoxEmulatorActivity : SDLActivity(), InputManager.InputDeviceListener {
  private lateinit var touchController: GdoxTouchController
  private lateinit var gameMenu: GdoxGameMenu
  private lateinit var menuControl: TextView
  private lateinit var inputManager: InputManager
  private var exiting = false
  private var destroyed = false
  private var gameReportedReady = false
  private var touchJoystickRegistered = false
  private var missingDiscObservations = 0
  @Volatile
  private var discWatchdogArmed = false
  private val discWatchdog = ScheduledThreadPoolExecutor(1) { task ->
    Thread(task, "gdox-disc-watchdog").apply { isDaemon = true }
  }.apply {
    removeOnCancelPolicy = true
  }
  private val sustainedPerformanceSupported by lazy {
    getSystemService(PowerManager::class.java)
      ?.isSustainedPerformanceModeSupported == true
  }
  private val backCallback =
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
      OnBackInvokedCallback { toggleGameMenu() }
    } else {
      null
  }
  private val usbHost by lazy {
    GdoxUsbHost(this) {
      Log.w(performanceLogTag, "physical drive disconnected; ending session")
      if (!GdoxSessionRecovery.recordDriveLoss(this)) {
        Log.w(performanceLogTag, "could not record the unexpected drive loss")
      }
      exitEmulator(force = true)
    }
  }
  private val thermalController by lazy {
    createGdoxThermalController(this)
  }

  private external fun nativeRequestExit()
  private external fun nativePhysicalDiscPresent(): Boolean

  @Suppress("unused")
  fun gdoxOpenPhysicalDrive(): Int = usbHost.openFileDescriptor()

  @Suppress("unused")
  fun gdoxClosePhysicalDrive() = usbHost.close()

  @Suppress("unused")
  fun gdoxApplyTitleProfile(titleId: Long, title: String) {
    val profile = GdoxEmulatorPolicy.activate(
      this,
      titleId.takeIf { it >= 0L },
      title
    )
    SDLActivity.nativeSetenv("XEMU_RENDERER", profile.renderer)
    startDiscWatchdog()
    runOnUiThread {
      if (!destroyed) {
        requestEmulatorFrameRate(profile.displayFrameRate)
        gameReportedReady = true
        reportGameState(isLoading = false)
      }
    }
  }

  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)
    reportGameState(isLoading = true)
    SDLActivity.nativeSetenv("SDL_ANDROID_TRAP_BACK_BUTTON", "1")
    usbHost.start()
    installTouchController()
    installGameMenu()
    installMenuControl()
    configureWindow()
    inputManager = getSystemService(Context.INPUT_SERVICE) as InputManager
    inputManager.registerInputDeviceListener(this, null)
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
      onBackInvokedDispatcher.registerOnBackInvokedCallback(
        OnBackInvokedDispatcher.PRIORITY_DEFAULT,
        backCallback!!
      )
    }
    mLayout?.post {
      if (!destroyed) updateTouchVisibility()
    }
  }

  override fun onResume() {
    super.onResume()
    thermalController.start()
    setSustainedPerformanceMode(enabled = true)
    reportGameState(isLoading = !gameReportedReady)
  }

  override fun onPause() {
    reportGameState(isLoading = false, mode = GameState.MODE_NONE)
    setSustainedPerformanceMode(enabled = false)
    thermalController.stop()
    super.onPause()
  }

  override fun getLibraries(): Array<String> = arrayOf("SDL2", "xemu")

  override fun dispatchKeyEvent(event: KeyEvent): Boolean {
    if (event.keyCode == KeyEvent.KEYCODE_BACK) {
      if (event.action == KeyEvent.ACTION_UP) toggleGameMenu()
      return true
    }
    return super.dispatchKeyEvent(event)
  }

  @Deprecated("Android 13 and newer use OnBackInvokedCallback")
  override fun onBackPressed() {
    toggleGameMenu()
  }

  override fun onInputDeviceAdded(deviceId: Int) = updateTouchVisibility()
  override fun onInputDeviceRemoved(deviceId: Int) = updateTouchVisibility()
  override fun onInputDeviceChanged(deviceId: Int) = updateTouchVisibility()

  override fun onDestroy() {
    val terminateProcess = isFinishing && !isChangingConfigurations
    destroyed = true
    discWatchdogArmed = false
    discWatchdog.shutdownNow()
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
      onBackInvokedDispatcher.unregisterOnBackInvokedCallback(backCallback!!)
    }
    if (::inputManager.isInitialized) {
      inputManager.unregisterInputDeviceListener(this)
    }
    if (::touchController.isInitialized) {
      touchController.visibility = View.GONE
    }
    if (touchJoystickRegistered) {
      SDLControllerManager.nativeRemoveJoystick(GdoxInputBridge.deviceId)
      touchJoystickRegistered = false
    }
    usbHost.stop()
    super.onDestroy()
    usbHost.close()
    if (terminateProcess) {
      android.os.Process.killProcess(android.os.Process.myPid())
    }
  }

  private fun configureWindow() {
    WindowCompat.setDecorFitsSystemWindows(window, false)
    requestEmulatorFrameRate(defaultDisplayFrameRate)
    WindowCompat.getInsetsController(window, window.decorView).apply {
      hide(WindowInsetsCompat.Type.systemBars())
      systemBarsBehavior =
        WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
    }
    ViewCompat.setOnApplyWindowInsetsListener(mLayout ?: return) { _, insets ->
      val safe = insets.getInsetsIgnoringVisibility(
        WindowInsetsCompat.Type.systemBars()
          or WindowInsetsCompat.Type.displayCutout()
      )
      val menuBarHeight = dp(24)
      SDLActivity.nativeSetenv(
        "GDOX_TOP_INSET_PX",
        (safe.top + menuBarHeight).toString()
      )
      touchController.setSafeInsets(
        safe.left,
        safe.top + menuBarHeight,
        safe.right,
        safe.bottom
      )
      (menuControl.layoutParams as ViewGroup.MarginLayoutParams).apply {
        topMargin = safe.top
        menuControl.layoutParams = this
      }
      insets
    }
    ViewCompat.requestApplyInsets(mLayout ?: return)
  }

  private fun requestEmulatorFrameRate(frameRate: Float) {
    window.attributes = window.attributes.apply {
      preferredRefreshRate = frameRate
    }
    if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R) return
    mSurface?.post {
      val surface = mSurface?.holder?.surface
      if (surface?.isValid != true) return@post
      try {
        surface.setFrameRate(
          frameRate,
          Surface.FRAME_RATE_COMPATIBILITY_DEFAULT
        )
        Log.i(performanceLogTag, "requested display rate=$frameRate")
      } catch (error: RuntimeException) {
        Log.w(performanceLogTag, "could not request emulator frame rate", error)
      }
    }
  }

  private fun installTouchController() {
    touchController = GdoxTouchController(this).apply {
      listener = GdoxInputBridge()
    }
    mLayout?.addView(
      touchController,
      RelativeLayout.LayoutParams(
        ViewGroup.LayoutParams.MATCH_PARENT,
        ViewGroup.LayoutParams.MATCH_PARENT
      )
    )
  }

  private fun installGameMenu() {
    gameMenu = GdoxGameMenu(
      this,
      onResumeGame = {
        gameMenu.close()
        menuControl.visibility = View.VISIBLE
        updateTouchVisibility()
      },
      onRestartGame = { exitEmulator(restart = true) },
      onExitGame = { exitEmulator() },
      onTouchControlsChanged = { updateTouchVisibility() },
      onThermalManagementChanged = { thermalController.refresh() }
    )
    mLayout?.addView(
      gameMenu,
      RelativeLayout.LayoutParams(
        ViewGroup.LayoutParams.MATCH_PARENT,
        ViewGroup.LayoutParams.MATCH_PARENT
      )
    )
  }

  private fun installMenuControl() {
    menuControl = TextView(this).apply {
      text = "GDOX"
      contentDescription = "Open GDOX in-game menu"
      gravity = Gravity.CENTER
      textSize = 10f
      letterSpacing = 0.16f
      typeface = Typeface.create("sans-serif", Typeface.BOLD)
      setTextColor(Color.argb(120, 255, 255, 255))
      setBackgroundColor(Color.BLACK)
      setOnClickListener { toggleGameMenu() }
    }
    mLayout?.addView(
      menuControl,
      RelativeLayout.LayoutParams(
        ViewGroup.LayoutParams.MATCH_PARENT,
        dp(24)
      ).apply {
        addRule(RelativeLayout.ALIGN_PARENT_TOP)
      }
    )
  }

  private fun updateTouchVisibility() {
    if (!::touchController.isInitialized || !::inputManager.isInitialized) return
    val physicalController = inputManager.inputDeviceIds.any { deviceId ->
      val device = inputManager.getInputDevice(deviceId) ?: return@any false
      val sources = device.sources
      val controller =
        sources and InputDevice.SOURCE_GAMEPAD == InputDevice.SOURCE_GAMEPAD ||
          sources and InputDevice.SOURCE_JOYSTICK == InputDevice.SOURCE_JOYSTICK
      controller && device.motionRanges.isNotEmpty()
    }
    val touchEnabled = GdoxCoreFiles.preferences(this).getBoolean(
      GdoxEmulatorPolicy.touchControlsKey,
      true
    )
    val shouldRegisterTouch = touchEnabled && !physicalController
    if (shouldRegisterTouch && !touchJoystickRegistered) {
      touchJoystickRegistered =
        SDLControllerManager.nativeAddJoystick(
          GdoxInputBridge.deviceId,
          "GDOX touch controller",
          "GDOX virtual Xbox controller",
          0x045e,
          0x028e,
          false,
          0xffff,
          6,
          0x3f,
          0,
          0
        ) >= 0
    } else if (!shouldRegisterTouch && touchJoystickRegistered) {
      SDLControllerManager.nativeRemoveJoystick(GdoxInputBridge.deviceId)
      touchJoystickRegistered = false
    }
    touchController.visibility =
      if (!touchJoystickRegistered || gameMenu.isOpen) {
        View.GONE
      } else {
        View.VISIBLE
      }
  }

  private fun toggleGameMenu() {
    if (gameMenu.isOpen) {
      gameMenu.close()
      menuControl.visibility = View.VISIBLE
      updateTouchVisibility()
    } else {
      touchController.visibility = View.GONE
      menuControl.visibility = View.GONE
      gameMenu.open()
    }
  }

  private fun exitEmulator(
    restart: Boolean = false,
    force: Boolean = false
  ) {
    if (exiting) return
    exiting = true
    discWatchdogArmed = false
    menuControl.isEnabled = false
    if (restart) {
      startActivity(
        Intent(this, GdoxActivity::class.java)
          .putExtra(GDOX_RESTART_EXTRA, true)
          .addFlags(
            Intent.FLAG_ACTIVITY_CLEAR_TOP or
              Intent.FLAG_ACTIVITY_SINGLE_TOP
          )
      )
    }
    if (!GdoxCoreFiles.clearOrderlySessionMarker(this)) {
      Log.w(
        performanceLogTag,
        "could not clear the orderly emulator-session marker"
      )
    }
    Thread {
      nativeRequestExit()
      if (force) {
        try {
          Thread.sleep(forcedExitDelayMs)
        } catch (_: InterruptedException) {
          Thread.currentThread().interrupt()
          return@Thread
        }
        Log.e(
          performanceLogTag,
          "emulator did not stop after disc loss; terminating xemu process"
        )
        android.os.Process.killProcess(android.os.Process.myPid())
      }
    }.start()
  }

  private fun startDiscWatchdog() {
    if (discWatchdogArmed || destroyed || exiting) return
    discWatchdogArmed = true
    discWatchdog.scheduleWithFixedDelay(
      {
        if (!discWatchdogArmed || destroyed || exiting) {
          return@scheduleWithFixedDelay
        }
        if (nativePhysicalDiscPresent()) {
          missingDiscObservations = 0
          return@scheduleWithFixedDelay
        }
        ++missingDiscObservations
        if (missingDiscObservations < missingDiscThreshold) {
          return@scheduleWithFixedDelay
        }
        discWatchdogArmed = false
        Log.w(performanceLogTag, "physical disc removed; ending session")
        runOnUiThread {
          if (!destroyed && !exiting) exitEmulator(force = true)
        }
      },
      discWatchdogInitialDelayMs,
      discWatchdogPollMs,
      TimeUnit.MILLISECONDS
    )
  }

  private fun reportGameState(
    isLoading: Boolean,
    mode: Int = GameState.MODE_GAMEPLAY_UNINTERRUPTIBLE
  ) {
    if (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU) return
    try {
      val manager = getSystemService(GameManager::class.java) ?: return
      manager.setGameState(GameState(isLoading, mode))
      Log.i(
        performanceLogTag,
        "gameMode=${manager.gameMode} loading=$isLoading state=$mode"
      )
    } catch (error: RuntimeException) {
      Log.w(performanceLogTag, "could not report game state", error)
    }
  }

  private fun setSustainedPerformanceMode(enabled: Boolean) {
    if (!sustainedPerformanceSupported) {
      if (enabled) {
        Log.i(performanceLogTag, "sustained performance mode unsupported")
      }
      return
    }
    window.setSustainedPerformanceMode(enabled)
    Log.i(performanceLogTag, "sustained performance mode enabled=$enabled")
  }

  private fun dp(value: Int): Int =
    (value * resources.displayMetrics.density).toInt()

  private companion object {
    const val defaultDisplayFrameRate = 60f
    const val discWatchdogInitialDelayMs = 500L
    const val discWatchdogPollMs = 500L
    const val missingDiscThreshold = 2
    const val forcedExitDelayMs = 1500L
    const val performanceLogTag = "GDOX-performance"
  }
}
