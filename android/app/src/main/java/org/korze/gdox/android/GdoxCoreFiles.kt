package org.korze.gdox.android

import android.content.Context
import android.content.SharedPreferences
import java.io.File

internal object GdoxCoreFiles {
  const val preferencesName = "gdox"
  private const val legacyPreferencesName = "x1box_prefs"
  private const val migrationKey = "gdox_preferences_migrated"
  private const val activeSessionMarkerName = "active_emulator_session.flag"
  private const val mcpxSize = 512L
  private val mcpx10Md5 = byteArrayOf(
    0xd4.toByte(), 0x9c.toByte(), 0x52, 0xa4.toByte(),
    0x10, 0x2f, 0x6d, 0xf7.toByte(),
    0xbc.toByte(), 0xf8.toByte(), 0xd0.toByte(), 0x61,
    0x7a, 0xc4.toByte(), 0x75, 0xed.toByte()
  )

  enum class Source(
    val title: String,
    val description: String,
    val pathKey: String,
    val uriKey: String,
    val fileName: String
  ) {
    MCPX(
      "MCPX ROM",
      "Your 512-byte MCPX boot ROM",
      "mcpxPath",
      "mcpxUri",
      "mcpx.bin"
    ),
    FLASH(
      "Xbox BIOS",
      "Your 1 MiB compatible Xbox flash ROM",
      "flashPath",
      "flashUri",
      "flash.bin"
    )
  }

  fun preferences(context: Context): SharedPreferences {
    val current = context.getSharedPreferences(
      preferencesName,
      Context.MODE_PRIVATE
    )
    if (current.getBoolean(migrationKey, false)) return current

    val legacy = context.getSharedPreferences(
      legacyPreferencesName,
      Context.MODE_PRIVATE
    )
    val editor = current.edit()
    legacy.all.forEach { (key, value) ->
      when (value) {
        is Boolean -> editor.putBoolean(key, value)
        is Float -> editor.putFloat(key, value)
        is Int -> editor.putInt(key, value)
        is Long -> editor.putLong(key, value)
        is String -> editor.putString(key, value)
        is Set<*> -> editor.putStringSet(
          key,
          value.filterIsInstance<String>().toSet()
        )
      }
    }
    editor.putBoolean(migrationKey, true).commit()
    return current
  }

  fun destination(context: Context, source: Source): File {
    val root = context.getExternalFilesDir(null) ?: context.filesDir
    return File(File(root, "gdox"), source.fileName)
  }

  fun clearOrderlySessionMarker(context: Context): Boolean {
    val root = context.getExternalFilesDir(null) ?: context.filesDir
    val marker = File(File(root, "gdox"), activeSessionMarkerName)
    if (!GdoxSecureFiles.pathExistsNoFollow(marker)) return true
    return GdoxSecureFiles.regularFileNoFollow(marker) &&
      GdoxSecureFiles.durableDelete(marker)
  }

  fun configured(context: Context): Boolean =
    Source.entries.all { ready(context, it) } && managedHdd(context) != null

  fun ready(context: Context, source: Source): Boolean {
    val value = preferences(context).getString(source.pathKey, null)
      ?: return false
    return valid(source, File(value))
  }

  fun valid(source: Source, file: File): Boolean {
    if (!file.isFile || !file.canRead()) return false
    return when (source) {
      Source.MCPX -> validMcpx(file)
      Source.FLASH -> file.length() == 1024L * 1024L
    }
  }

  fun managedHdd(context: Context): File? =
    GdoxManagedHdd.resolve(context, ::preferences)

  fun managedHddProblem(context: Context): String? =
    GdoxManagedHdd.problem(context)

  private fun validMcpx(file: File): Boolean {
    if (file.length() != mcpxSize) return false
    val digest = fileMd5(file) ?: return false
    return validMcpx10Digest(digest)
  }

  internal fun validMcpx10Digest(digest: ByteArray): Boolean =
    digest.contentEquals(mcpx10Md5)

  internal fun fileMd5(file: File): ByteArray? =
    GdoxSecureFiles.digest(file, "MD5")

  fun label(context: Context, source: Source): String {
    val value = preferences(context).getString(source.pathKey, null)
      ?: return "Not selected"
    val file = File(value)
    return if (valid(source, file)) {
      "${file.name} · ${formatSize(file.length())}"
    } else {
      "Unavailable"
    }
  }

  private fun formatSize(bytes: Long): String = when {
    bytes >= 1024L * 1024L * 1024L ->
      "%.1f GiB".format(bytes.toDouble() / (1024.0 * 1024.0 * 1024.0))
    bytes >= 1024L * 1024L ->
      "%.1f MiB".format(bytes.toDouble() / (1024.0 * 1024.0))
    bytes >= 1024L ->
      "%.1f KiB".format(bytes.toDouble() / 1024.0)
    else -> "$bytes bytes"
  }
}
