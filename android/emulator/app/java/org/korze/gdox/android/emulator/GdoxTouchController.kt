package org.korze.gdox.android.emulator

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.PointF
import android.view.MotionEvent
import android.view.View
import kotlin.math.hypot

internal class GdoxTouchController(context: Context) : View(context) {
  enum class Button {
    A, B, X, Y,
    UP, DOWN, LEFT, RIGHT,
    LEFT_TRIGGER, RIGHT_TRIGGER,
    START, BACK,
    LEFT_STICK, RIGHT_STICK,
    WHITE, BLACK
  }

  enum class Stick { LEFT, RIGHT }

  interface Listener {
    fun button(button: Button, pressed: Boolean)
    fun stick(stick: Stick, x: Float, y: Float)
  }

  private data class ButtonState(
    val center: PointF = PointF(),
    var radius: Float = 0f,
    var pointer: Int = noPointer,
    var pressed: Boolean = false
  )

  private data class StickState(
    val center: PointF = PointF(),
    val position: PointF = PointF(),
    var radius: Float = 0f,
    var pointer: Int = noPointer
  )

  var listener: Listener? = null
  private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
  private val buttons = Button.entries.associateWith { ButtonState() }
  private val sticks = Stick.entries.associateWith { StickState() }
  private var safeLeft = 0
  private var safeTop = 0
  private var safeRight = 0
  private var safeBottom = 0

  init {
    setBackgroundColor(Color.TRANSPARENT)
  }

  fun setSafeInsets(left: Int, top: Int, right: Int, bottom: Int) {
    if (safeLeft == left && safeTop == top &&
        safeRight == right && safeBottom == bottom) {
      return
    }
    safeLeft = left
    safeTop = top
    safeRight = right
    safeBottom = bottom
    layoutControls()
    invalidate()
  }

  override fun onSizeChanged(width: Int, height: Int, oldWidth: Int, oldHeight: Int) {
    super.onSizeChanged(width, height, oldWidth, oldHeight)
    layoutControls()
  }

  private fun layoutControls() {
    if (width <= 0 || height <= 0) return
    val left = safeLeft.toFloat()
    val top = safeTop.toFloat()
    val usableWidth = (width - safeLeft - safeRight).coerceAtLeast(1).toFloat()
    val usableHeight = (height - safeTop - safeBottom).coerceAtLeast(1).toFloat()
    fun place(button: Button, x: Float, y: Float, radius: Float) {
      buttons.getValue(button).apply {
        center.set(left + x * usableWidth, top + y * usableHeight)
        this.radius = radius * usableWidth
      }
    }

    val faceX = 0.875f
    val faceY = 0.56f
    val faceStep = 0.07f * usableWidth
    val faceRadius = 0.028f * usableWidth
    val faceCenterX = left + faceX * usableWidth
    val faceCenterY = top + faceY * usableHeight
    placeAt(Button.A, faceCenterX, faceCenterY + faceStep, faceRadius)
    placeAt(Button.B, faceCenterX + faceStep, faceCenterY, faceRadius)
    placeAt(Button.X, faceCenterX - faceStep, faceCenterY, faceRadius)
    placeAt(Button.Y, faceCenterX, faceCenterY - faceStep, faceRadius)

    val dpadX = 0.125f
    val dpadY = 0.45f
    val dpadStep = 0.05f * usableWidth
    val dpadRadius = 0.021f * usableWidth
    val dpadCenterX = left + dpadX * usableWidth
    val dpadCenterY = top + dpadY * usableHeight
    placeAt(Button.UP, dpadCenterX, dpadCenterY - dpadStep, dpadRadius)
    placeAt(Button.DOWN, dpadCenterX, dpadCenterY + dpadStep, dpadRadius)
    placeAt(Button.LEFT, dpadCenterX - dpadStep, dpadCenterY, dpadRadius)
    placeAt(Button.RIGHT, dpadCenterX + dpadStep, dpadCenterY, dpadRadius)

    place(Button.LEFT_TRIGGER, 0.055f, 0.11f, 0.034f)
    place(Button.RIGHT_TRIGGER, 0.945f, 0.11f, 0.034f)
    place(Button.BACK, 0.35f, 0.90f, 0.022f)
    place(Button.START, 0.41f, 0.90f, 0.022f)
    place(Button.LEFT_STICK, 0.08f, 0.90f, 0.022f)
    place(Button.RIGHT_STICK, 0.52f, 0.90f, 0.022f)
    place(Button.WHITE, 0.75f, 0.80f, 0.022f)
    place(Button.BLACK, 0.80f, 0.80f, 0.022f)

    sticks.getValue(Stick.LEFT).apply {
      center.set(left + 0.18f * usableWidth, top + 0.82f * usableHeight)
      radius = 0.07f * usableWidth
    }
    sticks.getValue(Stick.RIGHT).apply {
      center.set(left + 0.62f * usableWidth, top + 0.82f * usableHeight)
      radius = 0.07f * usableWidth
    }
  }

  private fun placeAt(
    button: Button,
    x: Float,
    y: Float,
    radius: Float
  ) {
    buttons.getValue(button).apply {
      center.set(x, y)
      this.radius = radius
    }
  }

  override fun onDraw(canvas: Canvas) {
    super.onDraw(canvas)
    sticks.forEach { (_, state) ->
      paint.style = Paint.Style.STROKE
      paint.strokeWidth = density(1.5f)
      paint.color = Color.argb(92, 255, 255, 255)
      canvas.drawCircle(state.center.x, state.center.y, state.radius, paint)
      paint.style = Paint.Style.FILL
      paint.color = Color.argb(100, 255, 255, 255)
      canvas.drawCircle(
        state.center.x + state.position.x * state.radius,
        state.center.y + state.position.y * state.radius,
        state.radius * 0.38f,
        paint
      )
    }
    buttons.forEach { (button, state) ->
      paint.style = Paint.Style.FILL
      paint.color = buttonColor(button, state.pressed)
      canvas.drawCircle(state.center.x, state.center.y, state.radius, paint)
      paint.style = Paint.Style.STROKE
      paint.strokeWidth = density(1.25f)
      paint.color = Color.argb(110, 255, 255, 255)
      canvas.drawCircle(state.center.x, state.center.y, state.radius, paint)
      paint.style = Paint.Style.FILL
      paint.color = Color.WHITE
      paint.textAlign = Paint.Align.CENTER
      paint.textSize = state.radius * 0.75f
      canvas.drawText(
        buttonLabel(button),
        state.center.x,
        state.center.y - (paint.ascent() + paint.descent()) * 0.5f,
        paint
      )
    }
  }

  override fun onTouchEvent(event: MotionEvent): Boolean {
    when (event.actionMasked) {
      MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
        val index = event.actionIndex
        press(event.getPointerId(index), event.getX(index), event.getY(index))
      }
      MotionEvent.ACTION_MOVE -> {
        for (index in 0 until event.pointerCount) {
          move(
            event.getPointerId(index),
            event.getX(index),
            event.getY(index)
          )
        }
      }
      MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> {
        release(event.getPointerId(event.actionIndex))
      }
      MotionEvent.ACTION_CANCEL -> reset()
    }
    invalidate()
    return true
  }

  private fun press(pointer: Int, x: Float, y: Float) {
    sticks.entries.firstOrNull { (_, state) ->
      state.pointer == noPointer && inside(x, y, state.center, state.radius)
    }?.let { (stick, state) ->
      state.pointer = pointer
      updateStick(stick, state, x, y)
      return
    }
    buttons.entries.firstOrNull { (_, state) ->
      state.pointer == noPointer && inside(x, y, state.center, state.radius)
    }?.let { (button, state) ->
      state.pointer = pointer
      state.pressed = true
      listener?.button(button, true)
    }
  }

  private fun move(pointer: Int, x: Float, y: Float) {
    sticks.entries.firstOrNull { it.value.pointer == pointer }?.let {
      updateStick(it.key, it.value, x, y)
    }
    buttons.entries.firstOrNull { it.value.pointer == pointer }?.let {
      val (button, state) = it
      val nowPressed = inside(x, y, state.center, state.radius * 1.35f)
      if (state.pressed != nowPressed) {
        state.pressed = nowPressed
        listener?.button(button, nowPressed)
      }
    }
  }

  private fun release(pointer: Int) {
    sticks.forEach { (stick, state) ->
      if (state.pointer == pointer) {
        state.pointer = noPointer
        state.position.set(0f, 0f)
        listener?.stick(stick, 0f, 0f)
      }
    }
    buttons.forEach { (button, state) ->
      if (state.pointer == pointer) {
        state.pointer = noPointer
        if (state.pressed) {
          state.pressed = false
          listener?.button(button, false)
        }
      }
    }
  }

  private fun reset() {
    sticks.forEach { (stick, state) ->
      if (state.pointer != noPointer) listener?.stick(stick, 0f, 0f)
      state.pointer = noPointer
      state.position.set(0f, 0f)
    }
    buttons.forEach { (button, state) ->
      if (state.pressed) listener?.button(button, false)
      state.pointer = noPointer
      state.pressed = false
    }
  }

  private fun updateStick(
    stick: Stick,
    state: StickState,
    x: Float,
    y: Float
  ) {
    val dx = x - state.center.x
    val dy = y - state.center.y
    val length = hypot(dx, dy)
    val scale = if (length > state.radius) state.radius / length else 1f
    var normalizedX = dx * scale / state.radius
    var normalizedY = dy * scale / state.radius
    if (hypot(normalizedX, normalizedY) < 0.12f) {
      normalizedX = 0f
      normalizedY = 0f
    }
    state.position.set(normalizedX, normalizedY)
    listener?.stick(stick, normalizedX, normalizedY)
  }

  override fun onDetachedFromWindow() {
    reset()
    super.onDetachedFromWindow()
  }

  override fun onVisibilityChanged(changedView: View, visibility: Int) {
    super.onVisibilityChanged(changedView, visibility)
    if (changedView === this && visibility != VISIBLE) reset()
  }

  private fun density(value: Float): Float =
    value * resources.displayMetrics.density

  private fun inside(
    x: Float,
    y: Float,
    center: PointF,
    radius: Float
  ): Boolean = hypot(x - center.x, y - center.y) <= radius

  private fun buttonColor(button: Button, pressed: Boolean): Int {
    val alpha = if (pressed) 220 else 110
    return when (button) {
      Button.A -> Color.argb(alpha, 16, 124, 16)
      Button.B -> Color.argb(alpha, 188, 45, 45)
      Button.X -> Color.argb(alpha, 42, 110, 190)
      Button.Y -> Color.argb(alpha, 210, 176, 36)
      Button.BLACK -> Color.argb(alpha, 24, 24, 24)
      Button.WHITE -> Color.argb(alpha, 210, 210, 210)
      else -> Color.argb(alpha, 78, 84, 80)
    }
  }

  private fun buttonLabel(button: Button): String = when (button) {
    Button.A -> "A"
    Button.B -> "B"
    Button.X -> "X"
    Button.Y -> "Y"
    Button.UP -> "↑"
    Button.DOWN -> "↓"
    Button.LEFT -> "←"
    Button.RIGHT -> "→"
    Button.LEFT_TRIGGER -> "LT"
    Button.RIGHT_TRIGGER -> "RT"
    Button.START -> "▶"
    Button.BACK -> "◀"
    Button.LEFT_STICK -> "LS"
    Button.RIGHT_STICK -> "RS"
    Button.WHITE -> "W"
    Button.BLACK -> "B"
  }

  private companion object {
    const val noPointer = -1
  }
}
