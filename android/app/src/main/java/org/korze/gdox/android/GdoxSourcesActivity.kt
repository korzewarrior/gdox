package org.korze.gdox.android

import android.app.Activity
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.view.Gravity
import android.view.View
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.TextView
import android.widget.Toast
import java.io.File
import java.io.FileOutputStream

class GdoxSourcesActivity : Activity() {
  private val rows = mutableMapOf<GdoxCoreFiles.Source, TextView>()
  private lateinit var progress: ProgressBar
  private lateinit var done: Button
  private var pending: GdoxCoreFiles.Source? = null
  private var copying = false

  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)
    setContentView(buildContent())
  }

  override fun onResume() {
    super.onResume()
    refresh()
  }

  @Deprecated("Uses the platform document picker result contract.")
  override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
    super.onActivityResult(requestCode, resultCode, data)
    if (requestCode != pickFileRequest || resultCode != RESULT_OK) {
      pending = null
      return
    }
    val source = pending ?: return
    val uri = data?.data ?: return
    pending = null
    val flags = data.flags and
      (Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION)
    try {
      contentResolver.takePersistableUriPermission(
        uri,
        flags and Intent.FLAG_GRANT_READ_URI_PERMISSION
      )
    } catch (_: SecurityException) {
    }
    copySource(source, uri)
  }

  private fun buildContent(): View {
    val content = GdoxUi.content(this)
    content.addView(GdoxUi.heading(this, "Sources"))
    content.addView(
      GdoxUi.body(
        this,
        "Choose the two console files required by the emulator. GDOX provides " +
          "its clean, read-only system drive."
      ),
      margin(top = 8, bottom = 26)
    )
    GdoxCoreFiles.managedHddProblem(this)?.let { problem ->
      content.addView(
        GdoxUi.body(this, problem),
        margin(bottom = 18)
      )
    }

    GdoxCoreFiles.Source.entries.forEach { source ->
      content.addView(sourceRow(source), margin(bottom = 14))
    }

    progress = ProgressBar(this).apply {
      isIndeterminate = true
      visibility = View.GONE
    }
    content.addView(
      progress,
      LinearLayout.LayoutParams(wrap, wrap).apply {
        gravity = Gravity.CENTER_HORIZONTAL
        topMargin = GdoxUi.dp(this@GdoxSourcesActivity, 8)
        bottomMargin = GdoxUi.dp(this@GdoxSourcesActivity, 8)
      }
    )

    done = Button(this).apply {
      text = "Done"
      GdoxUi.primary(this)
      setOnClickListener { finish() }
    }
    content.addView(done, margin(top = 12))
    return GdoxUi.scrollable(this, content)
  }

  private fun sourceRow(source: GdoxCoreFiles.Source): View {
    val row = LinearLayout(this).apply {
      orientation = LinearLayout.VERTICAL
      setPadding(
        GdoxUi.dp(this@GdoxSourcesActivity, 18),
        GdoxUi.dp(this@GdoxSourcesActivity, 16),
        GdoxUi.dp(this@GdoxSourcesActivity, 18),
        GdoxUi.dp(this@GdoxSourcesActivity, 16)
      )
      GdoxUi.card(this)
    }
    row.addView(GdoxUi.heading(this, source.title, 18f))
    row.addView(GdoxUi.body(this, source.description), margin(top = 3))
    val status = GdoxUi.body(this, "").apply {
      setTextColor(GdoxUi.xboxGreen)
      maxLines = 2
    }
    rows[source] = status
    row.addView(status, margin(top = 10))
    row.addView(Button(this).apply {
      text = "Choose file"
      GdoxUi.secondary(this)
      setOnClickListener { choose(source) }
    }, margin(top = 10))
    return row
  }

  private fun choose(source: GdoxCoreFiles.Source) {
    if (copying) return
    pending = source
    val intent = Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
      addCategory(Intent.CATEGORY_OPENABLE)
      type = "application/octet-stream"
      addFlags(
        Intent.FLAG_GRANT_READ_URI_PERMISSION or
          Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION
      )
    }
    @Suppress("DEPRECATION")
    startActivityForResult(intent, pickFileRequest)
  }

  private fun copySource(source: GdoxCoreFiles.Source, uri: Uri) {
    copying = true
    refresh()
    Thread {
      val destination = GdoxCoreFiles.destination(this, source)
      val temporary = File(destination.parentFile, "${destination.name}.part")
      var failureMessage: String? = null
      try {
        destination.parentFile?.mkdirs()
        contentResolver.openInputStream(uri)?.use { input ->
          FileOutputStream(temporary).use { output -> input.copyTo(output) }
        } ?: error("The selected file could not be opened")
        if (!GdoxCoreFiles.valid(source, temporary)) {
          error("The selected file has the wrong size or format")
        }
        if (destination.exists() && !destination.delete()) {
          error("The previous file could not be replaced")
        }
        if (!temporary.renameTo(destination)) {
          error("The selected file could not be stored")
        }
        GdoxCoreFiles.preferences(this).edit()
          .putString(source.pathKey, destination.absolutePath)
          .remove(source.uriKey)
          .apply()
      } catch (failure: Exception) {
        temporary.delete()
        failureMessage = failure.message ?: "The selected file could not be imported"
      }
      runOnUiThread {
        copying = false
        refresh()
        failureMessage?.let {
          Toast.makeText(this, it, Toast.LENGTH_LONG).show()
        }
      }
    }.start()
  }

  private fun refresh() {
    rows.forEach { (source, view) ->
      view.text = GdoxCoreFiles.label(this, source)
      view.setTextColor(
        if (GdoxCoreFiles.ready(this, source)) GdoxUi.xboxGreen else GdoxUi.muted
      )
    }
    if (::progress.isInitialized) {
      progress.visibility = if (copying) View.VISIBLE else View.GONE
    }
    if (::done.isInitialized) {
      done.isEnabled = !copying
      done.text = if (GdoxCoreFiles.configured(this)) "Done" else "Back"
    }
  }

  private fun margin(top: Int = 0, bottom: Int = 0) =
    LinearLayout.LayoutParams(match, wrap).apply {
      topMargin = GdoxUi.dp(this@GdoxSourcesActivity, top)
      bottomMargin = GdoxUi.dp(this@GdoxSourcesActivity, bottom)
    }

  private companion object {
    const val pickFileRequest = 4107
    const val match = LinearLayout.LayoutParams.MATCH_PARENT
    const val wrap = LinearLayout.LayoutParams.WRAP_CONTENT
  }
}
