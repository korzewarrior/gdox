package org.korze.gdox.android

import java.io.File
import java.nio.file.Files
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class GdoxCoreFilesTest {
  @get:Rule
  val temporaryFolder = TemporaryFolder()

  @Test
  fun acceptsExactMcpx10Digest() {
    assertTrue(GdoxCoreFiles.validMcpx10Digest(mcpx10Md5()))
  }

  @Test
  fun rejectsDifferent512ByteMcpxFile() {
    val file = temporaryFile("invalid-mcpx.bin", 512)
    val digest = GdoxCoreFiles.fileMd5(file)

    assertTrue(digest != null && digest.contentEquals(byteArrayOf(
      0xbf.toByte(), 0x61, 0x9e.toByte(), 0xac.toByte(),
      0x0c, 0xdf.toByte(), 0x3f, 0x68,
      0xd4.toByte(), 0x96.toByte(), 0xea.toByte(), 0x93.toByte(),
      0x44, 0x13, 0x7e, 0x8b.toByte()
    )))
    assertFalse(GdoxCoreFiles.valid(GdoxCoreFiles.Source.MCPX, file))
  }

  @Test
  fun rejectsWrongMcpxSizeBeforeHashing() {
    val file = temporaryFile("short-mcpx.bin", 511)

    assertFalse(GdoxCoreFiles.valid(GdoxCoreFiles.Source.MCPX, file))
  }

  @Test
  fun preservesOneMiBAndroidBiosPolicy() {
    val valid = temporaryFile("bios.bin", 1024 * 1024)
    val desktopOnlySize = temporaryFile("bios-512k.bin", 512 * 1024)

    assertTrue(GdoxCoreFiles.valid(GdoxCoreFiles.Source.FLASH, valid))
    assertFalse(GdoxCoreFiles.valid(GdoxCoreFiles.Source.FLASH, desktopOnlySize))
  }

  @Test
  fun rejectsUnpinnedManagedHardDisk() {
    val unpinned = temporaryFile("xbox_hdd.qcow2", 1638400)

    assertFalse(GdoxManagedHdd.valid(unpinned))
  }

  @Test
  fun rejectsMissingSourceFile() {
    val missing = File(temporaryFolder.root, "missing.bin")

    assertFalse(GdoxCoreFiles.valid(GdoxCoreFiles.Source.MCPX, missing))
  }

  @Test
  fun cleanFirstInstallDoesNotEnterMigration() {
    val plan = managedHddPlan(cleanNames = emptySet())

    assertEquals(
      GdoxManagedHdd.Action.INSTALL_CANONICAL,
      plan.action
    )
    assertNull(plan.legacy)
    assertNull(plan.problem)
  }

  @Test
  fun uriFallbackRawDiskMigratesBesideCleanCanonicalDisk() {
    val plan = managedHddPlan(
      presentNames = setOf("xbox_hdd.qcow2", "hdd.img"),
      cleanNames = setOf("xbox_hdd.qcow2")
    )

    assertEquals("xbox_hdd.qcow2", plan.active?.name)
    assertEquals("hdd.img", plan.legacy?.name)
    assertEquals(GdoxManagedHdd.Action.NONE, plan.action)
    assertNull(plan.problem)
  }

  @Test
  fun normalPickerQcow2MigratesBesideCleanCanonicalDisk() {
    val plan = managedHddPlan(
      presentNames = setOf("xbox_hdd.qcow2", "hdd.qcow2"),
      cleanNames = setOf("xbox_hdd.qcow2")
    )

    assertEquals("xbox_hdd.qcow2", plan.active?.name)
    assertEquals("hdd.qcow2", plan.legacy?.name)
    assertNull(plan.problem)
  }

  @Test
  fun cleanCanonicalWithoutLegacyHasNoMigrationSource() {
    val plan = managedHddPlan(
      presentNames = setOf("xbox_hdd.qcow2"),
      cleanNames = setOf("xbox_hdd.qcow2")
    )

    assertEquals("xbox_hdd.qcow2", plan.active?.name)
    assertNull(plan.legacy)
    assertEquals(GdoxManagedHdd.Action.NONE, plan.action)
  }

  @Test
  fun modifiedCanonicalDiskUsesOnlyPinnedStagedBacking() {
    val needsInstall = managedHddPlan(
      presentNames = setOf("xbox_hdd.qcow2"),
      cleanNames = emptySet()
    )
    val ready = managedHddPlan(
      presentNames = setOf("xbox_hdd.qcow2", "xbox_hdd.clean.qcow2"),
      cleanNames = setOf("xbox_hdd.clean.qcow2")
    )

    assertEquals(
      GdoxManagedHdd.Action.INSTALL_STAGED,
      needsInstall.action
    )
    assertEquals("xbox_hdd.clean.qcow2", ready.active?.name)
    assertEquals("xbox_hdd.qcow2", ready.legacy?.name)
    assertNull(ready.problem)
  }

  @Test
  fun ambiguousLegacyDisksFailWithoutSelectingEither() {
    val plan = managedHddPlan(
      presentNames = setOf("hdd.img", "xbox_hdd.qcow2"),
      cleanNames = emptySet()
    )

    assertNull(plan.active)
    assertNull(plan.legacy)
    assertTrue(plan.problem?.contains("preserved") == true)
  }

  @Test
  fun pickerAndUriFallbackCopiesTogetherAreAmbiguousAndUnchanged() {
    val picker = temporaryFile("hdd.qcow2", 32).apply {
      writeBytes(ByteArray(32) { 0x2a.toByte() })
    }
    val fallback = temporaryFile("hdd.img", 48).apply {
      writeBytes(ByteArray(48) { 0x6b.toByte() })
    }
    val pickerBefore = picker.readBytes()
    val fallbackBefore = fallback.readBytes()
    val plan = managedHddPlan(
      presentNames = setOf(
        "xbox_hdd.qcow2",
        picker.name,
        fallback.name
      ),
      cleanNames = setOf("xbox_hdd.qcow2")
    )

    assertTrue(plan.problem != null)
    assertNull(plan.active)
    assertNull(plan.legacy)
    assertTrue(picker.readBytes().contentEquals(pickerBefore))
    assertTrue(fallback.readBytes().contentEquals(fallbackBefore))
  }

  @Test
  fun interruptedPromotionRequiresDurableAuthorization() {
    val blocked = managedHddPlan(
      presentNames = setOf("xbox_hdd.clean.qcow2"),
      cleanNames = setOf("xbox_hdd.clean.qcow2")
    )
    val authorized = managedHddPlan(
      presentNames = setOf("xbox_hdd.clean.qcow2"),
      cleanNames = setOf("xbox_hdd.clean.qcow2"),
      promotionAuthorized = true
    )

    assertTrue(blocked.problem != null)
    assertEquals(
      GdoxManagedHdd.Action.PROMOTE_STAGED,
      authorized.action
    )
  }

  @Test
  fun unsafeManagedNameIsNeverFollowed() {
    val plan = GdoxManagedHdd.plan(
      temporaryFolder.root,
      present = { it.name == "hdd.img" },
      regular = { false },
      clean = { false }
    )

    assertNull(plan.active)
    assertTrue(plan.problem != null)
  }

  @Test
  fun legacyManagedDiskSymlinkIsPreservedAndRejected() {
    val external = temporaryFolder.newFile("external-hdd.img").apply {
      writeText("preserve")
    }
    val legacy = temporaryFolder.root.resolve("hdd.img")
    Files.createSymbolicLink(legacy.toPath(), external.toPath())

    val plan = GdoxManagedHdd.plan(temporaryFolder.root)

    assertNull(plan.active)
    assertNull(plan.legacy)
    assertTrue(plan.problem != null)
    assertTrue(Files.isSymbolicLink(legacy.toPath()))
    assertEquals("preserve", external.readText())
  }

  @Test
  fun interruptedStagedDiskSymlinkIsPreservedAndRejected() {
    val external = temporaryFolder.newFile("external-clean.qcow2").apply {
      writeText("preserve")
    }
    val staged = temporaryFolder.root.resolve("xbox_hdd.clean.qcow2")
    Files.createSymbolicLink(staged.toPath(), external.toPath())

    val plan = GdoxManagedHdd.plan(
      temporaryFolder.root,
      promotionAuthorized = true
    )

    assertNull(plan.active)
    assertNull(plan.legacy)
    assertTrue(plan.problem != null)
    assertTrue(Files.isSymbolicLink(staged.toPath()))
    assertEquals("preserve", external.readText())
  }

  private fun temporaryFile(name: String, size: Int): File =
    temporaryFolder.newFile(name).apply { writeBytes(ByteArray(size)) }

  private fun managedHddPlan(
    cleanNames: Set<String>,
    presentNames: Set<String> = cleanNames,
    promotionAuthorized: Boolean = false
  ): GdoxManagedHdd.Plan = GdoxManagedHdd.plan(
    temporaryFolder.root,
    present = { it.name in presentNames },
    regular = { it.name in presentNames },
    clean = { it.name in cleanNames },
    promotionAuthorized = promotionAuthorized
  )

  private fun mcpx10Md5(): ByteArray = byteArrayOf(
    0xd4.toByte(), 0x9c.toByte(), 0x52, 0xa4.toByte(),
    0x10, 0x2f, 0x6d, 0xf7.toByte(),
    0xbc.toByte(), 0xf8.toByte(), 0xd0.toByte(), 0x61,
    0x7a, 0xc4.toByte(), 0x75, 0xed.toByte()
  )
}
