package org.korze.gdox.android.emulator

import android.content.Context
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.widget.AdapterView
import android.widget.ArrayAdapter
import android.widget.Button
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.Spinner
import android.widget.Switch
import android.widget.TextView
import org.korze.gdox.android.GdoxCoreFiles
import org.korze.gdox.android.GdoxEmulatorPolicy
import org.korze.gdox.android.GdoxUi

internal class GdoxGameMenu(
  context: Context,
  private val onResumeGame: () -> Unit,
  private val onRestartGame: () -> Unit,
  private val onExitGame: () -> Unit,
  private val onTouchControlsChanged: () -> Unit,
  private val onThermalManagementChanged: () -> Unit
) : FrameLayout(context) {
  private val preferences = GdoxCoreFiles.preferences(context)

  val isOpen: Boolean
    get() = visibility == View.VISIBLE

  init {
    visibility = View.GONE
    isClickable = true
    isFocusable = true
    setBackgroundColor(Color.argb(214, 4, 7, 5))

    val content = LinearLayout(context).apply {
      orientation = LinearLayout.VERTICAL
      setPadding(dp(24), dp(22), dp(24), dp(22))
      background = GradientDrawable().apply {
        setColor(GdoxUi.surface)
        cornerRadius = dp(20).toFloat()
        setStroke(dp(1), GdoxUi.outline)
      }
    }
    content.addView(TextView(context).apply {
      text = "GDOX"
      textSize = 28f
      letterSpacing = 0.08f
      typeface = Typeface.create("sans-serif", Typeface.BOLD)
      setTextColor(GdoxUi.text)
    })
    content.addView(label("IN-GAME"), margin(top = 2, bottom = 10))

    content.addView(Button(context).apply {
      text = "Resume"
      GdoxUi.primary(this)
      setOnClickListener { onResumeGame() }
    }, margin(bottom = 16))

    content.addView(label("VIDEO"), margin(bottom = 6))
    content.addView(caption("Internal resolution"))
    content.addView(picker(
      listOf("1× · fastest", "2×", "3×", "4×"),
      preferences.getInt(GdoxEmulatorPolicy.surfaceScaleKey, 1)
        .coerceIn(1, 4) - 1
    ) { position ->
      preferences.edit()
        .putInt(GdoxEmulatorPolicy.surfaceScaleKey, position + 1)
        .apply()
    })
    content.addView(caption("Screen shape"), margin(top = 10))
    content.addView(picker(
      listOf("Original 4:3", "Widescreen · supported games"),
      if (preferences.getString(
          GdoxEmulatorPolicy.aspectRatioKey,
          "4:3"
        ) == "16:9") 1 else 0
    ) { position ->
      preferences.edit()
        .putString(
          GdoxEmulatorPolicy.aspectRatioKey,
          if (position == 1) "16:9" else "4:3"
        )
        .apply()
    })
    content.addView(caption("Image filtering"), margin(top = 10))
    content.addView(picker(
      listOf("Smooth", "Sharp pixels"),
      if (preferences.getString(
          GdoxEmulatorPolicy.filteringKey,
          "linear"
        ) == "nearest") 1 else 0
    ) { position ->
      preferences.edit()
        .putString(
          GdoxEmulatorPolicy.filteringKey,
          if (position == 1) "nearest" else "linear"
        )
        .apply()
    })
    content.addView(Switch(context).apply {
      text = "Vertical sync"
      textSize = 16f
      setTextColor(GdoxUi.text)
      minHeight = dp(48)
      isChecked = preferences.getBoolean(GdoxEmulatorPolicy.vsyncKey, false)
      setOnCheckedChangeListener { _, checked ->
        preferences.edit()
          .putBoolean(GdoxEmulatorPolicy.vsyncKey, checked)
          .apply()
      }
    }, margin(top = 6))
    content.addView(caption(
      "Video changes are applied by restarting the game."
    ), margin(top = 8, bottom = 12))

    content.addView(label("PERFORMANCE"), margin(bottom = 4))
    content.addView(Switch(context).apply {
      text = "Manage heat"
      textSize = 16f
      setTextColor(GdoxUi.text)
      minHeight = dp(48)
      isChecked = preferences.getBoolean(
        GdoxEmulatorPolicy.thermalManagementKey,
        true
      )
      setOnCheckedChangeListener { _, checked ->
        preferences.edit()
          .putBoolean(GdoxEmulatorPolicy.thermalManagementKey, checked)
          .apply()
        onThermalManagementChanged()
      }
    }, margin(bottom = 12))

    content.addView(label("INPUT"), margin(bottom = 4))
    content.addView(Switch(context).apply {
      text = "Touch controls"
      textSize = 16f
      setTextColor(GdoxUi.text)
      minHeight = dp(48)
      isChecked = preferences.getBoolean(
        GdoxEmulatorPolicy.touchControlsKey,
        true
      )
      setOnCheckedChangeListener { _, checked ->
        preferences.edit()
          .putBoolean(GdoxEmulatorPolicy.touchControlsKey, checked)
          .apply()
        onTouchControlsChanged()
      }
    })

    content.addView(Button(context).apply {
      text = "Restart game"
      GdoxUi.secondary(this)
      setOnClickListener { onRestartGame() }
    }, margin(top = 14))
    content.addView(Button(context).apply {
      text = "Exit game"
      GdoxUi.secondary(this)
      setOnClickListener { onExitGame() }
    }, margin(top = 8))

    val scroll = ScrollView(context).apply {
      isFillViewport = false
      overScrollMode = View.OVER_SCROLL_IF_CONTENT_SCROLLS
      addView(
        content,
        ViewGroup.LayoutParams(
          ViewGroup.LayoutParams.MATCH_PARENT,
          ViewGroup.LayoutParams.WRAP_CONTENT
        )
      )
    }
    addView(
      scroll,
      LayoutParams(dp(390), ViewGroup.LayoutParams.MATCH_PARENT).apply {
        gravity = Gravity.CENTER
        topMargin = dp(18)
        bottomMargin = dp(18)
      }
    )
  }

  fun open() {
    visibility = View.VISIBLE
    requestFocus()
  }

  fun close() {
    visibility = View.GONE
  }

  private fun picker(
    values: List<String>,
    selection: Int,
    onSelected: (Int) -> Unit
  ): Spinner = Spinner(context).apply {
    adapter = ArrayAdapter(
      context,
      android.R.layout.simple_spinner_dropdown_item,
      values
    )
    minimumHeight = dp(48)
    setSelection(selection, false)
    onItemSelectedListener = SimpleSelectionListener(onSelected)
  }

  private fun label(value: String): TextView = TextView(context).apply {
    text = value
    textSize = 11f
    letterSpacing = 0.14f
    typeface = Typeface.create("sans-serif", Typeface.BOLD)
    setTextColor(GdoxUi.xboxGreen)
  }

  private fun caption(value: String): TextView = TextView(context).apply {
    text = value
    textSize = 13f
    setTextColor(GdoxUi.muted)
  }

  private fun margin(
    top: Int = 0,
    bottom: Int = 0
  ) = LinearLayout.LayoutParams(
    ViewGroup.LayoutParams.MATCH_PARENT,
    ViewGroup.LayoutParams.WRAP_CONTENT
  ).apply {
    topMargin = dp(top)
    bottomMargin = dp(bottom)
  }

  private fun dp(value: Int): Int =
    (value * resources.displayMetrics.density).toInt()
}

private class SimpleSelectionListener(
  private val onSelected: (Int) -> Unit
) : AdapterView.OnItemSelectedListener {
  override fun onItemSelected(
    parent: AdapterView<*>?,
    view: View?,
    position: Int,
    id: Long
  ) = onSelected(position)

  override fun onNothingSelected(parent: AdapterView<*>?) = Unit
}
