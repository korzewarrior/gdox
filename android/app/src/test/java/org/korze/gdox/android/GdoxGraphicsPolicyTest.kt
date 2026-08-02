package org.korze.gdox.android

import org.junit.Assert.assertEquals
import org.junit.Test

class GdoxGraphicsPolicyTest {
  private fun preferences(
    surfaceScale: Int = 3,
    aspectRatio: String = "16:9",
    filtering: String = "nearest",
    vsync: Boolean = false,
    compatibilityProfiles: Boolean = true
  ) = GdoxGraphicsPreferences(
    surfaceScale,
    aspectRatio,
    filtering,
    vsync,
    compatibilityProfiles
  )

  @Test
  fun unknownTitleUsesValidatedUserSettings() {
    val resolved = GdoxGraphicsPolicy.resolve(
      preferences(surfaceScale = 8, aspectRatio = "invalid", filtering = "invalid"),
      0xFFFFFFFFL
    )

    assertEquals(4, resolved.surfaceScale)
    assertEquals("4:3", resolved.aspectRatio)
    assertEquals("linear", resolved.filtering)
    assertEquals(60f, resolved.displayFrameRate, 0f)
    assertEquals("User settings", resolved.profileName)
  }

  @Test
  fun knownTitleAppliesPerformanceProfile() {
    val resolved = GdoxGraphicsPolicy.resolve(preferences(), 0x4D530004L)

    assertEquals(1, resolved.surfaceScale)
    assertEquals(30f, resolved.displayFrameRate, 0f)
    assertEquals("Halo performance", resolved.profileName)
  }

  @Test
  fun vsyncKeepsDisplayAtNativeRate() {
    val resolved = GdoxGraphicsPolicy.resolve(
      preferences(vsync = true),
      0x4D530004L
    )

    assertEquals(60f, resolved.displayFrameRate, 0f)
  }

  @Test
  fun disabledProfilesPreserveUserScale() {
    val resolved = GdoxGraphicsPolicy.resolve(
      preferences(compatibilityProfiles = false),
      0x4D530004L
    )

    assertEquals(3, resolved.surfaceScale)
    assertEquals("User settings", resolved.profileName)
  }
}
