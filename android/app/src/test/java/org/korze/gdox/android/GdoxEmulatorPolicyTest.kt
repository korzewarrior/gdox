package org.korze.gdox.android

import java.nio.file.Files
import java.nio.file.LinkOption
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class GdoxEmulatorPolicyTest {
  @get:Rule
  val temporaryFolder = TemporaryFolder()

  @Test
  fun removesManagedCacheWithoutFollowingSymlink() {
    val external = temporaryFolder.newFolder("external")
    val protected = external.resolve("keep.bin").apply {
      writeText("preserve")
    }
    val cache = temporaryFolder.newFolder("shaders")
    cache.resolve("local.bin").writeText("discard")
    Files.createSymbolicLink(
      cache.toPath().resolve("external-link"),
      external.toPath()
    )

    assertTrue(GdoxEmulatorPolicy.removeLegacyCacheTree(cache))
    assertFalse(cache.exists())
    assertTrue(protected.readText() == "preserve")
  }

  @Test
  fun removesOnlyTheSymlinkWhenCacheRootIsLinked() {
    val external = temporaryFolder.newFolder("external-root")
    val protected = external.resolve("keep.bin").apply {
      writeText("preserve")
    }
    val cacheLink = temporaryFolder.root.resolve("shader_cache_list")
    Files.createSymbolicLink(cacheLink.toPath(), external.toPath())

    assertTrue(GdoxEmulatorPolicy.removeLegacyCacheTree(cacheLink))
    assertFalse(Files.exists(cacheLink.toPath(), LinkOption.NOFOLLOW_LINKS))
    assertTrue(protected.readText() == "preserve")
  }
}
