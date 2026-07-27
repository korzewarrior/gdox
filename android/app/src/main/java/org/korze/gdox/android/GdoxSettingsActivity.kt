package org.korze.gdox.android

import android.app.Activity
import android.os.Bundle
import android.view.View
import android.widget.AdapterView
import android.widget.ArrayAdapter
import android.widget.Button
import android.widget.LinearLayout
import android.widget.Spinner
import android.widget.Switch
import android.widget.TextView
import android.widget.Toast

class GdoxSettingsActivity : Activity() {
  private val preferences by lazy { GdoxCoreFiles.preferences(this) }
  private lateinit var scale: Spinner
  private lateinit var aspect: Spinner
  private lateinit var filtering: Spinner
  private lateinit var profiles: Switch
  private lateinit var autoStart: Switch
  private lateinit var vsync: Switch
  private lateinit var thermalManagement: Switch
  private lateinit var activeProfile: TextView
  private var binding = false

  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)
    setContentView(buildContent())
    bindPreferences()
  }

  override fun onResume() {
    super.onResume()
    updateActiveProfile()
  }

  private fun buildContent(): View {
    val content = GdoxUi.content(this)
    content.addView(GdoxUi.heading(this, "Settings"))
    content.addView(
      GdoxUi.body(
        this,
        "Compatibility profiles keep known-sensitive games on safe rendering settings."
      ),
      LinearLayout.LayoutParams(match, wrap).apply {
        topMargin = GdoxUi.dp(this@GdoxSettingsActivity, 8)
        bottomMargin = GdoxUi.dp(this@GdoxSettingsActivity, 26)
      }
    )

    autoStart = addSwitch(
      content,
      "Auto start",
      "Start when the supported drive and emulator files are ready."
    )
    profiles = addSwitch(
      content,
      "Game compatibility profiles",
      "Apply tested limits for titles such as Morrowind."
    )
    scale = addPicker(
      content,
      "Internal resolution",
      listOf("1×", "2×", "3×", "4×")
    )
    aspect = addPicker(
      content,
      "Screen shape",
      listOf("Original 4:3", "Widescreen (supported games)")
    )
    filtering = addPicker(
      content,
      "Image filtering",
      listOf("Smooth", "Sharp pixels")
    )
    vsync = addSwitch(
      content,
      "Vertical sync",
      "Off uses title-matched output for lower heat. Enable for tear-free 60 Hz."
    )
    thermalManagement = addSwitch(
      content,
      "Manage heat",
      "Reduce screen brightness only when the device gets hot."
    )
    activeProfile = GdoxUi.body(this, "")
    content.addView(
      activeProfile,
      LinearLayout.LayoutParams(match, wrap).apply {
        topMargin = GdoxUi.dp(this@GdoxSettingsActivity, 14)
      }
    )

    content.addView(Button(this).apply {
      text = "Clear emulator caches"
      GdoxUi.secondary(this)
      setOnClickListener {
        val removed = GdoxEmulatorPolicy.clearCaches(this@GdoxSettingsActivity)
        Toast.makeText(
          this@GdoxSettingsActivity,
          if (removed == 0) "Caches are already clean" else "Emulator caches cleared",
          Toast.LENGTH_SHORT
        ).show()
      }
    }, buttonParams(top = 24))

    content.addView(Button(this).apply {
      text = "Restore graphics defaults"
      GdoxUi.secondary(this)
      setOnClickListener {
        GdoxEmulatorPolicy.resetGraphics(preferences)
        bindPreferences()
      }
    }, buttonParams(top = 10))

    content.addView(Button(this).apply {
      text = "Done"
      GdoxUi.primary(this)
      setOnClickListener { finish() }
    }, buttonParams(top = 18))
    return GdoxUi.scrollable(this, content)
  }

  private fun addSwitch(
    content: LinearLayout,
    title: String,
    description: String
  ): Switch {
    val control = Switch(this).apply {
      text = title
      textSize = 16f
      setTextColor(GdoxUi.text)
      minHeight = GdoxUi.dp(this@GdoxSettingsActivity, 48)
    }
    content.addView(control, LinearLayout.LayoutParams(match, wrap).apply {
      topMargin = GdoxUi.dp(this@GdoxSettingsActivity, 16)
    })
    content.addView(
      GdoxUi.body(this, description),
      LinearLayout.LayoutParams(match, wrap).apply {
        bottomMargin = GdoxUi.dp(this@GdoxSettingsActivity, 4)
      }
    )
    return control
  }

  private fun addPicker(
    content: LinearLayout,
    title: String,
    values: List<String>
  ): Spinner {
    content.addView(
      GdoxUi.sectionLabel(this, title),
      LinearLayout.LayoutParams(match, wrap).apply {
        topMargin = GdoxUi.dp(this@GdoxSettingsActivity, 22)
      }
    )
    val picker = Spinner(this).apply {
      adapter = ArrayAdapter(
        this@GdoxSettingsActivity,
        android.R.layout.simple_spinner_dropdown_item,
        values
      )
      minimumHeight = GdoxUi.dp(this@GdoxSettingsActivity, 52)
    }
    content.addView(picker, LinearLayout.LayoutParams(match, wrap).apply {
      topMargin = GdoxUi.dp(this@GdoxSettingsActivity, 4)
    })
    return picker
  }

  private fun bindPreferences() {
    binding = true
    autoStart.isChecked = preferences.getBoolean(
      GdoxEmulatorPolicy.autoStartKey,
      true
    )
    profiles.isChecked = preferences.getBoolean(
      GdoxEmulatorPolicy.compatibilityProfilesKey,
      true
    )
    scale.setSelection(
      preferences.getInt(GdoxEmulatorPolicy.surfaceScaleKey, 1)
        .coerceIn(1, 4) - 1
    )
    aspect.setSelection(
      when (preferences.getString(GdoxEmulatorPolicy.aspectRatioKey, "4:3")) {
        "16:9" -> 1
        else -> 0
      }
    )
    filtering.setSelection(
      if (preferences.getString(GdoxEmulatorPolicy.filteringKey, "linear")
          == "nearest") 1 else 0
    )
    vsync.isChecked = preferences.getBoolean(
      GdoxEmulatorPolicy.vsyncKey,
      false
    )
    thermalManagement.isChecked = preferences.getBoolean(
      GdoxEmulatorPolicy.thermalManagementKey,
      true
    )
    binding = false

    autoStart.setOnCheckedChangeListener { _, checked ->
      if (!binding) preferences.edit()
        .putBoolean(GdoxEmulatorPolicy.autoStartKey, checked).apply()
    }
    profiles.setOnCheckedChangeListener { _, checked ->
      if (!binding) preferences.edit()
        .putBoolean(GdoxEmulatorPolicy.compatibilityProfilesKey, checked).apply()
    }
    scale.onItemSelectedListener = selectionListener {
      preferences.edit().putInt(
        GdoxEmulatorPolicy.surfaceScaleKey,
        scale.selectedItemPosition + 1
      ).apply()
    }
    aspect.onItemSelectedListener = selectionListener {
      preferences.edit().putString(
        GdoxEmulatorPolicy.aspectRatioKey,
        when (aspect.selectedItemPosition) {
          1 -> "16:9"
          else -> "4:3"
        }
      ).apply()
    }
    filtering.onItemSelectedListener = selectionListener {
      preferences.edit().putString(
        GdoxEmulatorPolicy.filteringKey,
        if (filtering.selectedItemPosition == 1) "nearest" else "linear"
      ).apply()
    }
    vsync.setOnCheckedChangeListener { _, checked ->
      if (!binding) preferences.edit()
        .putBoolean(GdoxEmulatorPolicy.vsyncKey, checked).apply()
    }
    thermalManagement.setOnCheckedChangeListener { _, checked ->
      if (!binding) preferences.edit()
        .putBoolean(GdoxEmulatorPolicy.thermalManagementKey, checked).apply()
    }
    updateActiveProfile()
  }

  private fun selectionListener(onSelected: () -> Unit) =
    object : AdapterView.OnItemSelectedListener {
      override fun onItemSelected(
        parent: AdapterView<*>?,
        view: View?,
        position: Int,
        id: Long
      ) {
        if (!binding) onSelected()
      }

      override fun onNothingSelected(parent: AdapterView<*>?) = Unit
    }

  private fun updateActiveProfile() {
    if (!::activeProfile.isInitialized) return
    val profile = preferences.getString("gdox_active_profile", null)
    val title = preferences.getString("gdox_active_title", null)
    activeProfile.text = if (profile != null && title != null) {
      "Last session: $title · $profile"
    } else {
      "The active profile is resolved from the disc when play starts."
    }
  }

  private fun buttonParams(top: Int) =
    LinearLayout.LayoutParams(match, wrap).apply {
      topMargin = GdoxUi.dp(this@GdoxSettingsActivity, top)
    }

  private companion object {
    const val match = LinearLayout.LayoutParams.MATCH_PARENT
    const val wrap = LinearLayout.LayoutParams.WRAP_CONTENT
  }
}
