package org.korze.gdox.android.emulator

import android.view.KeyEvent
import org.libsdl.app.SDLControllerManager

internal class GdoxInputBridge : GdoxTouchController.Listener {
  override fun button(button: GdoxTouchController.Button, pressed: Boolean) {
    val key = when (button) {
      GdoxTouchController.Button.A -> KeyEvent.KEYCODE_BUTTON_A
      GdoxTouchController.Button.B -> KeyEvent.KEYCODE_BUTTON_B
      GdoxTouchController.Button.X -> KeyEvent.KEYCODE_BUTTON_X
      GdoxTouchController.Button.Y -> KeyEvent.KEYCODE_BUTTON_Y
      GdoxTouchController.Button.UP -> KeyEvent.KEYCODE_DPAD_UP
      GdoxTouchController.Button.DOWN -> KeyEvent.KEYCODE_DPAD_DOWN
      GdoxTouchController.Button.LEFT -> KeyEvent.KEYCODE_DPAD_LEFT
      GdoxTouchController.Button.RIGHT -> KeyEvent.KEYCODE_DPAD_RIGHT
      GdoxTouchController.Button.LEFT_TRIGGER -> KeyEvent.KEYCODE_BUTTON_L2
      GdoxTouchController.Button.RIGHT_TRIGGER -> KeyEvent.KEYCODE_BUTTON_R2
      GdoxTouchController.Button.START -> KeyEvent.KEYCODE_BUTTON_START
      GdoxTouchController.Button.BACK -> KeyEvent.KEYCODE_BUTTON_SELECT
      GdoxTouchController.Button.LEFT_STICK -> KeyEvent.KEYCODE_BUTTON_THUMBL
      GdoxTouchController.Button.RIGHT_STICK -> KeyEvent.KEYCODE_BUTTON_THUMBR
      GdoxTouchController.Button.WHITE -> KeyEvent.KEYCODE_BUTTON_L1
      GdoxTouchController.Button.BLACK -> KeyEvent.KEYCODE_BUTTON_R1
    }
    if (pressed) {
      SDLControllerManager.onNativePadDown(deviceId, key)
    } else {
      SDLControllerManager.onNativePadUp(deviceId, key)
    }
  }

  override fun stick(stick: GdoxTouchController.Stick, x: Float, y: Float) {
    val firstAxis = when (stick) {
      GdoxTouchController.Stick.LEFT -> 0
      GdoxTouchController.Stick.RIGHT -> 2
    }
    SDLControllerManager.onNativeJoy(deviceId, firstAxis, x)
    SDLControllerManager.onNativeJoy(deviceId, firstAxis + 1, y)
  }

  companion object {
    const val deviceId = -2
  }
}
