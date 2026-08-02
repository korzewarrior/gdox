package org.korze.gdox.android

import android.content.Context
import android.content.SharedPreferences
import android.os.Build
import android.util.Log
import java.io.File
import java.util.Locale

internal const val GDOX_RESTART_EXTRA =
  "org.korze.gdox.extra.RESTART_GAME"

internal object GdoxEmulatorPolicy {
  const val surfaceScaleKey = "gdox_surface_scale"
  const val aspectRatioKey = "gdox_aspect_ratio"
  const val filteringKey = "gdox_filtering"
  const val vsyncKey = "gdox_vsync"
  const val thermalManagementKey = "gdox_thermal_management"
  const val touchControlsKey = "gdox_touch_controls"
  const val compatibilityProfilesKey = "gdox_compatibility_profiles"
  const val autoStartKey = "gdox_auto_start"

  private const val cacheSignatureKey = "gdox_emulator_cache_signature"
  private const val cacheEpoch = 2

  fun resolve(
    preferences: SharedPreferences,
    titleId: Long?
  ): GdoxResolvedGraphics = GdoxGraphicsPolicy.resolve(
    GdoxGraphicsPreferences(
      surfaceScale = preferences.getInt(surfaceScaleKey, 1),
      aspectRatio = preferences.getString(aspectRatioKey, "4:3") ?: "4:3",
      filtering = preferences.getString(filteringKey, "linear") ?: "linear",
      vsync = preferences.getBoolean(vsyncKey, false),
      compatibilityProfiles = preferences.getBoolean(
        compatibilityProfilesKey,
        true
      )
    ),
    titleId
  )

  fun activate(
    context: Context,
    titleId: Long?,
    title: String
  ): GdoxResolvedGraphics {
    val preferences = GdoxCoreFiles.preferences(context)
    val resolved = resolve(preferences, titleId)
    val signature = cacheSignature(context, resolved)
    if (preferences.getString(cacheSignatureKey, null) != signature) {
      clearCaches(context)
    }

    val editor = preferences.edit()
    editor
      .putString("runtime_override_renderer", resolved.renderer)
      .putString("runtime_override_surface_scale", resolved.surfaceScale.toString())
      .putString("runtime_override_aspect_ratio", resolved.aspectRatio)
      .putString("runtime_override_filtering", resolved.filtering)
      .putString("runtime_override_vsync", resolved.vsync.toString())
      .putString(cacheSignatureKey, signature)
      .putString("gdox_active_title", title)
      .putString(
        "gdox_active_title_id",
        titleId?.let { String.format(Locale.ROOT, "%08X", it) } ?: "unknown"
      )
      .putString("gdox_active_profile", resolved.profileName)
      .commit()

    Log.i(
      "GDOX-profile",
      "title=${titleId?.let { String.format(Locale.ROOT, "%08X", it) } ?: "unknown"} " +
        "profile=${resolved.profileName} renderer=${resolved.renderer} " +
        "scale=${resolved.surfaceScale} aspect=${resolved.aspectRatio} " +
        "filter=${resolved.filtering} vsync=${resolved.vsync} " +
        "display=${resolved.displayFrameRate.toInt()}Hz"
    )
    return resolved
  }

  fun clearCaches(context: Context): Int {
    val targets = mutableListOf<File>()
    context.getExternalFilesDir(null)?.let {
      val emulatorRoot = File(it, "gdox")
      targets += File(emulatorRoot, "shaders")
      targets += File(emulatorRoot, "shader_cache_list")
    }
    var removed = 0
    targets.distinctBy { it.absolutePath }.forEach { target ->
      if (target.exists() && target.deleteRecursively()) {
        removed++
      }
    }
    GdoxCoreFiles.preferences(context).edit()
      .remove(cacheSignatureKey)
      .apply()
    Log.i("GDOX-profile", "cleared $removed emulator cache location(s)")
    return removed
  }

  fun resetGraphics(preferences: SharedPreferences) {
    preferences.edit()
      .putInt(surfaceScaleKey, 1)
      .putString(aspectRatioKey, "4:3")
      .putString(filteringKey, "linear")
      .putBoolean(compatibilityProfilesKey, true)
      .remove(vsyncKey)
      .remove("runtime_override_vsync")
      .remove(cacheSignatureKey)
      .apply()
  }

  private fun cacheSignature(
    context: Context,
    settings: GdoxResolvedGraphics
  ): String {
    val packageInfo = context.packageManager.getPackageInfo(
      context.packageName,
      0
    )
    val version = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
      packageInfo.longVersionCode
    } else {
      @Suppress("DEPRECATION")
      packageInfo.versionCode.toLong()
    }
    return listOf(
      cacheEpoch,
      version,
      packageInfo.lastUpdateTime,
      settings.renderer,
      settings.surfaceScale,
      settings.aspectRatio,
      settings.filtering,
      settings.vsync,
      "fp0",
      "jit0",
      "tier0"
    ).joinToString(":")
  }
}
