package org.korze.gdox.android

import android.content.Context
import android.content.SharedPreferences
import android.util.Log
import java.io.File
import java.io.IOException
import java.nio.file.FileVisitResult
import java.nio.file.Files
import java.nio.file.LinkOption
import java.nio.file.Path
import java.nio.file.SimpleFileVisitor
import java.nio.file.attribute.BasicFileAttributes
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
    removeLegacyCaches(context)

    val editor = preferences.edit()
    editor
      .putString("runtime_override_renderer", resolved.renderer)
      .putString("runtime_override_surface_scale", resolved.surfaceScale.toString())
      .putString("runtime_override_aspect_ratio", resolved.aspectRatio)
      .putString("runtime_override_filtering", resolved.filtering)
      .putString("runtime_override_vsync", resolved.vsync.toString())
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

  private fun removeLegacyCaches(context: Context) {
    val targets = mutableListOf<File>()
    context.getExternalFilesDir(null)?.let {
      val emulatorRoot = File(it, "gdox")
      targets += File(emulatorRoot, "shaders")
      targets += File(emulatorRoot, "shader_cache_list")
    }
    var removed = 0
    targets.distinctBy { it.absolutePath }.forEach { target ->
      if (removeLegacyCacheTree(target)) removed++
    }
    if (removed != 0) {
      Log.i("GDOX-storage", "removed $removed legacy cache location(s)")
    }
  }

  internal fun removeLegacyCacheTree(target: File): Boolean {
    val root = target.toPath()
    if (!Files.exists(root, LinkOption.NOFOLLOW_LINKS)) return false
    return try {
      Files.walkFileTree(root, object : SimpleFileVisitor<Path>() {
        override fun visitFile(
          file: Path,
          attributes: BasicFileAttributes
        ): FileVisitResult {
          Files.delete(file)
          return FileVisitResult.CONTINUE
        }

        override fun postVisitDirectory(
          directory: Path,
          failure: IOException?
        ): FileVisitResult {
          if (failure != null) throw failure
          Files.delete(directory)
          return FileVisitResult.CONTINUE
        }
      })
      true
    } catch (_: IOException) {
      false
    } catch (_: SecurityException) {
      false
    }
  }

  fun resetGraphics(preferences: SharedPreferences) {
    preferences.edit()
      .putInt(surfaceScaleKey, 1)
      .putString(aspectRatioKey, "4:3")
      .putString(filteringKey, "linear")
      .putBoolean(compatibilityProfilesKey, true)
      .remove(vsyncKey)
      .remove("runtime_override_vsync")
      .apply()
  }
}
