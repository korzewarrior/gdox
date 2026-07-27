package org.korze.gdox.android

import android.app.Activity
import android.content.Context
import android.content.res.ColorStateList
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import androidx.core.view.ViewCompat
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.doOnAttach
import kotlin.math.min

internal object GdoxUi {
  const val background = 0xff090c0a.toInt()
  const val surface = 0xff141a16.toInt()
  const val surfaceRaised = 0xff1b231d.toInt()
  const val text = 0xfff5f7f5.toInt()
  const val muted = 0xffa6aea8.toInt()
  const val xboxGreen = 0xff107c10.toInt()
  const val outline = 0xff303a32.toInt()

  fun dp(context: Context, value: Int): Int =
    (value * context.resources.displayMetrics.density).toInt()

  fun compactHeight(context: Context): Boolean =
    context.resources.configuration.screenHeightDp < 500

  fun configureWindow(activity: Activity, root: View) {
    WindowCompat.setDecorFitsSystemWindows(activity.window, false)
    root.doOnAttach { view ->
      WindowCompat.getInsetsController(activity.window, view).apply {
        isAppearanceLightStatusBars = false
        isAppearanceLightNavigationBars = false
      }
    }
    ViewCompat.setOnApplyWindowInsetsListener(root) { view, insets ->
      val safe = insets.getInsets(
        WindowInsetsCompat.Type.systemBars()
          or WindowInsetsCompat.Type.displayCutout()
      )
      view.setPadding(safe.left, safe.top, safe.right, safe.bottom)
      insets
    }
    ViewCompat.requestApplyInsets(root)
  }

  fun content(context: Context): LinearLayout =
    WidthLimitedColumn(context, dp(context, 600)).apply {
      orientation = LinearLayout.VERTICAL
      val vertical = if (compactHeight(context)) 10 else 22
      setPadding(dp(context, 24), dp(context, vertical), dp(context, 24), dp(context, vertical))
    }

  fun scrollable(activity: Activity, content: LinearLayout): View {
    val root = FrameLayout(activity).apply {
      setBackgroundColor(GdoxUi.background)
    }
    val scroll = ScrollView(activity).apply {
      isFillViewport = true
      overScrollMode = View.OVER_SCROLL_IF_CONTENT_SCROLLS
      isScrollbarFadingEnabled = true
    }
    val frame = FrameLayout(activity)
    frame.addView(
      content,
      FrameLayout.LayoutParams(
        ViewGroup.LayoutParams.MATCH_PARENT,
        ViewGroup.LayoutParams.WRAP_CONTENT
      ).apply { gravity = Gravity.TOP or Gravity.CENTER_HORIZONTAL }
    )
    scroll.addView(
      frame,
      ViewGroup.LayoutParams(
        ViewGroup.LayoutParams.MATCH_PARENT,
        ViewGroup.LayoutParams.WRAP_CONTENT
      )
    )
    root.addView(
      scroll,
      FrameLayout.LayoutParams(
        ViewGroup.LayoutParams.MATCH_PARENT,
        ViewGroup.LayoutParams.MATCH_PARENT
      )
    )
    configureWindow(activity, root)
    return root
  }

  fun heading(context: Context, value: String, size: Float = 32f): TextView =
    TextView(context).apply {
      text = value
      textSize = size
      typeface = Typeface.create("sans-serif", Typeface.BOLD)
      setTextColor(GdoxUi.text)
      setLineSpacing(0f, 1.08f)
    }

  fun body(context: Context, value: String): TextView =
    TextView(context).apply {
      text = value
      textSize = 14f
      setTextColor(muted)
      setLineSpacing(0f, 1.28f)
    }

  fun card(view: View) {
    view.background = GradientDrawable().apply {
      setColor(surface)
      cornerRadius = dp(view.context, 18).toFloat()
      setStroke(dp(view.context, 1), outline)
    }
  }

  fun primary(button: Button) {
    button.apply {
      isAllCaps = false
      textSize = 16f
      typeface = Typeface.create("sans-serif", Typeface.BOLD)
      setTextColor(Color.WHITE)
      backgroundTintList = ColorStateList.valueOf(xboxGreen)
      minHeight = dp(context, 56)
      setPadding(dp(context, 18), dp(context, 12), dp(context, 18), dp(context, 12))
      stateListAnimator = null
    }
  }

  fun secondary(button: Button) {
    button.apply {
      isAllCaps = false
      textSize = 15f
      setTextColor(GdoxUi.text)
      backgroundTintList = ColorStateList.valueOf(surfaceRaised)
      minHeight = dp(context, 52)
      setPadding(dp(context, 16), dp(context, 10), dp(context, 16), dp(context, 10))
      stateListAnimator = null
    }
  }

  fun sectionLabel(context: Context, value: String): TextView =
    TextView(context).apply {
      text = value.uppercase()
      textSize = 11f
      typeface = Typeface.create("sans-serif", Typeface.BOLD)
      setTextColor(xboxGreen)
      letterSpacing = 0.14f
    }

  private class WidthLimitedColumn(
    context: Context,
    private val maximumWidth: Int
  ) : LinearLayout(context) {
    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
      val available = View.MeasureSpec.getSize(widthMeasureSpec)
      val limitedWidth = min(available, maximumWidth)
      super.onMeasure(
        View.MeasureSpec.makeMeasureSpec(limitedWidth, View.MeasureSpec.EXACTLY),
        heightMeasureSpec
      )
    }
  }
}
