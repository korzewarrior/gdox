package org.korze.gdox.android

import android.content.Context
import java.io.File

internal object GdoxSessionRecovery {
  private const val driveLossMarkerName = "unexpected_drive_loss.flag"

  fun recordDriveLoss(context: Context): Boolean {
    val marker = driveLossMarker(context)
    return marker.parentFile?.let { it.isDirectory || it.mkdirs() } == true &&
      (marker.isFile || marker.createNewFile())
  }

  fun driveLossPending(context: Context): Boolean =
    driveLossMarker(context).isFile

  fun acknowledgeDriveLoss(context: Context): Boolean {
    val marker = driveLossMarker(context)
    return !marker.exists() || marker.delete()
  }

  private fun driveLossMarker(context: Context): File {
    val root = context.getExternalFilesDir(null) ?: context.filesDir
    return File(File(root, "gdox"), driveLossMarkerName)
  }
}
