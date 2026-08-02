package org.korze.gdox.android

internal data class GdoxResolvedGraphics(
  val renderer: String,
  val surfaceScale: Int,
  val aspectRatio: String,
  val filtering: String,
  val vsync: Boolean,
  val displayFrameRate: Float,
  val profileName: String
)

internal data class GdoxGraphicsPreferences(
  val surfaceScale: Int,
  val aspectRatio: String,
  val filtering: String,
  val vsync: Boolean,
  val compatibilityProfiles: Boolean
)

internal object GdoxGraphicsPolicy {
  private data class GameProfile(
    val name: String,
    val surfaceScale: Int,
    val displayFrameRate: Float
  )

  private const val defaultDisplayFrameRate = 60f
  private val gameProfiles = mapOf(
    0x42530005L to GameProfile("Morrowind compatibility", 1, 30f),
    0x4D530004L to GameProfile("Halo performance", 1, 30f),
    0x4D5300D1L to GameProfile("Fable performance", 1, 30f)
  )

  fun resolve(
    preferences: GdoxGraphicsPreferences,
    titleId: Long?
  ): GdoxResolvedGraphics {
    val scale = preferences.surfaceScale.coerceIn(1, 4)
    val aspect = preferences.aspectRatio.takeIf { it == "4:3" || it == "16:9" }
      ?: "4:3"
    val filtering = preferences.filtering.takeIf {
      it == "linear" || it == "nearest"
    } ?: "linear"
    val profile = if (preferences.compatibilityProfiles) {
      titleId?.let(gameProfiles::get)
    } else {
      null
    }
    return GdoxResolvedGraphics(
      renderer = "vulkan",
      surfaceScale = profile?.surfaceScale ?: scale,
      aspectRatio = aspect,
      filtering = filtering,
      vsync = preferences.vsync,
      displayFrameRate = if (profile != null && !preferences.vsync) {
        profile.displayFrameRate
      } else {
        defaultDisplayFrameRate
      },
      profileName = profile?.name ?: "User settings"
    )
  }
}
