"""Regression tests for Android build provenance and patch validation."""

from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / "scripts"))

from android_patchset import ensure_applied, validate_applied

GRADLE_DISTRIBUTION_SHA256 = (
    "a17ddd85a26b6a7f5ddb71ff8b05fc5104c0202c6e64782429790c933686c806"
)
GRADLE_WRAPPER_JAR_SHA256 = (
    "76805e32c009c0cf0dd5d206bddc9fb22ea42e84db904b764f3047de095493f3"
)


def git(repository: Path, *arguments: str) -> None:
    subprocess.run(
        ["git", "-C", str(repository), *arguments],
        check=True,
        capture_output=True,
    )


def initialize_repository(repository: Path) -> None:
    repository.mkdir()
    git(repository, "init", "--quiet")
    git(repository, "config", "user.email", "tests@example.com")
    git(repository, "config", "user.name", "GDOX tests")
    (repository / "source.txt").write_text("base\n", encoding="utf-8")
    git(repository, "add", "source.txt")
    git(repository, "commit", "--quiet", "-m", "base")


def create_patch_series(patch_root: Path) -> None:
    patch_root.mkdir()
    (patch_root / "series").write_text("0001-test.patch\n", encoding="utf-8")
    (patch_root / "0001-test.patch").write_bytes(
        b"""diff --git a/source.txt b/source.txt
--- a/source.txt
+++ b/source.txt
@@ -1 +1 @@
-base
+patched
diff --git a/new.txt b/new.txt
new file mode 100644
--- /dev/null
+++ b/new.txt
@@ -0,0 +1 @@
+new
""",
    )


def shell_assignments(path: Path) -> dict[str, str]:
    assignments: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line or line.startswith("#"):
            continue
        key, separator, value = line.partition("=")
        if separator:
            assignments[key] = value
    return assignments


class AndroidBuildProvenanceTest(unittest.TestCase):
    def test_android_and_desktop_share_the_canonical_tagline(self) -> None:
        canonical = (
            "made by korze, with love, for gaming preservation everywhere"
        )
        android_activity = (
            ROOT
            / "android/app/src/main/java/org/korze/gdox/android/GdoxActivity.kt"
        ).read_text(encoding="utf-8")
        desktop_presentation = (
            ROOT / "src/ui/presentation.cpp"
        ).read_text(encoding="utf-8")

        self.assertEqual(android_activity.count(f'"{canonical}"'), 1)
        self.assertEqual(desktop_presentation.count(f'"{canonical}"'), 1)

    def test_android_uses_only_the_pinned_managed_hdd(self) -> None:
        core_files = (
            ROOT
            / "android/app/src/main/java/org/korze/gdox/android/GdoxCoreFiles.kt"
        ).read_text(encoding="utf-8")
        managed_hdd = (
            ROOT
            / "android/app/src/main/java/org/korze/gdox/android/"
            "GdoxManagedHdd.kt"
        ).read_text(encoding="utf-8")
        secure_files = (
            ROOT
            / "android/app/src/main/java/org/korze/gdox/android/"
            "GdoxSecureFiles.kt"
        ).read_text(encoding="utf-8")
        sources = (
            ROOT
            / "android/app/src/main/java/org/korze/gdox/android/GdoxSourcesActivity.kt"
        ).read_text(encoding="utf-8")
        build = (ROOT / "scripts/build_android.sh").read_text(encoding="utf-8")
        platform_patch = (
            ROOT
            / "android/emulator/patches/0002-gdox-android-platform.patch"
        ).read_text(encoding="utf-8")

        self.assertIn(
            '"00d7df7a2bc235f8801764f00b7f40e194d1e392f7a9619d6b2396c89770f6dd"',
            managed_hdd,
        )
        self.assertNotIn("Source.HDD", core_files + sources)
        storage_graph = core_files + managed_hdd + secure_files
        self.assertNotIn("legacy.delete()", storage_graph)
        self.assertNotIn("destination.delete()", storage_graph)
        self.assertNotIn("temporary.delete()", storage_graph)
        self.assertNotIn("renameTo(", storage_graph)
        for required in (
            'stagedManagedHddName = "xbox_hdd.clean.qcow2"',
            'legacyManagedHddNames = listOf("hdd.qcow2", "hdd.img")',
            'managedHddRoleKey = "gdox_managed_hdd_role"',
            'legacyManagedHddRoleKey = "gdox_legacy_managed_hdd_role"',
            'editor.putString(legacyManagedHddRoleKey, "hdd-qcow2")',
            'editor.putString(legacyManagedHddRoleKey, "hdd-img")',
            "fun plan(",
            "GdoxSecureFiles.ensureOwnedDirectory(directory)",
            "INSTALL_STAGED",
            "promotionAuthorized = validPromotionMarker(context)",
            '"managed_hdd_promotion_v1.complete"',
            '"gdox-managed-hdd-promotion-v1\\n"',
            "GdoxSecureFiles.copyExclusive(input, temporary)",
            "GdoxSecureFiles.durableRename(temporary, destination)",
            "GdoxSecureFiles.durableDelete(temporary)",
            "if (plan.legacy == null)",
            "clearLegacyHddPreferences(context, preferences)",
            "plan.active.name == stagedManagedHddName",
            "plan.legacy?.name == managedHddName",
        ):
            self.assertIn(required, managed_hdd)
        for required in (
            "LinkOption.NOFOLLOW_LINKS",
            "OsConstants.O_EXCL",
            "OsConstants.O_NOFOLLOW",
            "OsConstants.S_ISDIR(status.st_mode)",
            "status.st_uid == Process.myUid()",
            "Os.fsync(output.fd)",
            "Os.fsync(openDescriptor)",
            "Os.rename(source.absolutePath, destination.absolutePath)",
            "Os.remove(file.absolutePath)",
        ):
            self.assertIn(required, secure_files)
        self.assertNotIn('getString("hddPath"', core_files + managed_hdd)
        self.assertNotIn('getString("hddUri"', core_files + managed_hdd)
        self.assertIn('GDOX_ASSETS_DIR=$assets_root', build)
        self.assertIn('assets.setSrcDirs(listOf(gdoxAssetsDir))', platform_patch)
        self.assertIn('out.hdd = base + "/xbox_hdd.qcow2"', platform_patch)
        self.assertNotIn('GetPrefString(env, activity, "hddUri")', platform_patch)
        self.assertNotIn('GetPrefString(env, activity, "hddPath")', platform_patch)
        self.assertNotIn("gdox_android_hdd_reset_cache", platform_patch)

    def test_android_core_file_responsibilities_are_isolated(self) -> None:
        source_root = (
            ROOT / "android/app/src/main/java/org/korze/gdox/android"
        )
        facade = (source_root / "GdoxCoreFiles.kt").read_text(encoding="utf-8")
        managed = (source_root / "GdoxManagedHdd.kt").read_text(
            encoding="utf-8"
        )
        secure = (source_root / "GdoxSecureFiles.kt").read_text(
            encoding="utf-8"
        )

        self.assertLessEqual(len(facade.splitlines()), 240)
        self.assertLessEqual(len(managed.splitlines()), 360)
        self.assertLessEqual(len(secure.splitlines()), 220)
        self.assertIn("GdoxManagedHdd.resolve(context, ::preferences)", facade)
        self.assertIn("GdoxManagedHdd.problem(context)", facade)
        self.assertIn("GdoxSecureFiles.digest(file, \"MD5\")", facade)
        for forbidden in (
            "android.system.",
            "java.nio.file.",
            "MessageDigest",
            "OsConstants",
            "managedHddSha256",
            "managedHddPromotionMarkerName",
        ):
            self.assertNotIn(forbidden, facade)
        for forbidden in (
            "preferencesName",
            "legacyPreferencesName",
            "MCPX ROM",
            "Xbox BIOS",
            "formatSize(",
            "MessageDigest",
            "android.system.",
        ):
            self.assertNotIn(forbidden, managed)
        for forbidden in (
            "SharedPreferences",
            "managedHdd",
            "MCPX",
            "formatSize(",
        ):
            self.assertNotIn(forbidden, secure)

    def test_android_ci_supplies_every_required_gradle_path(self) -> None:
        workflow = (ROOT / ".github/workflows/ci.yml").read_text(
            encoding="utf-8"
        )

        self.assertIn('mkdir -p "$RUNNER_TEMP/gdox-assets"', workflow)
        for property_name in (
            "gdoxSourceDir",
            "gdoxAssetsDir",
            "gdoxLibusbSourceDir",
            "gdoxSdl2SourceDir",
        ):
            self.assertIn(f"-P{property_name}=", workflow)

    def test_emulator_cache_cleanup_is_automatic(self) -> None:
        policy = (
            ROOT
            / "android/app/src/main/java/org/korze/gdox/android/GdoxEmulatorPolicy.kt"
        ).read_text(encoding="utf-8")
        settings = (
            ROOT
            / "android/app/src/main/java/org/korze/gdox/android/GdoxSettingsActivity.kt"
        ).read_text(encoding="utf-8")
        emulator_activity = (
            ROOT
            / "android/emulator/app/java/org/korze/gdox/android/emulator/"
            "GdoxEmulatorActivity.kt"
        ).read_text(encoding="utf-8")

        self.assertIn("removeLegacyCaches(context)", policy)
        self.assertIn('File(emulatorRoot, "shaders")', policy)
        self.assertIn('File(emulatorRoot, "shader_cache_list")', policy)
        self.assertIn("Files.walkFileTree", policy)
        self.assertIn("LinkOption.NOFOLLOW_LINKS", policy)
        self.assertNotIn("deleteRecursively", policy)
        self.assertNotIn("cacheSignature", policy)
        self.assertNotIn("Clear emulator caches", settings)
        self.assertIn("fun gdoxShowStorageStatus(message: String)", emulator_activity)
        self.assertIn("Toast.LENGTH_LONG", emulator_activity)

    def test_cached_patch_tree_is_revalidated(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            repository = root / "repository"
            patch_root = root / "patches"
            initialize_repository(repository)
            create_patch_series(patch_root)

            ensure_applied(repository, patch_root)
            (repository / ".gdox-patch-set").write_text(
                "cached\n", encoding="utf-8"
            )
            (repository / "android" / ".gradle").mkdir(parents=True)
            (repository / "android" / ".gradle" / "state").write_text(
                "generated\n", encoding="utf-8"
            )
            (repository / "android" / "local.properties").write_text(
                "sdk.dir=/temporary\n", encoding="utf-8"
            )

            validate_applied(repository, patch_root)
            ensure_applied(repository, patch_root)

            (repository / "source.txt").write_text(
                "tampered\n", encoding="utf-8"
            )
            with self.assertRaisesRegex(RuntimeError, "does not exactly match"):
                validate_applied(repository, patch_root)
            with self.assertRaisesRegex(RuntimeError, "changes that do not match"):
                ensure_applied(repository, patch_root)

            (repository / "source.txt").write_text(
                "patched\n", encoding="utf-8"
            )
            (repository / "unexpected.txt").write_text(
                "unexpected\n", encoding="utf-8"
            )
            with self.assertRaisesRegex(RuntimeError, "does not exactly match"):
                validate_applied(repository, patch_root)

    def test_gradle_provenance_is_pinned_and_enforced(self) -> None:
        dependencies = shell_assignments(ROOT / "android/dependencies.lock")
        self.assertEqual(
            dependencies["GRADLE_DISTRIBUTION_SHA256"],
            GRADLE_DISTRIBUTION_SHA256,
        )
        self.assertEqual(
            dependencies["GRADLE_WRAPPER_JAR_SHA256"],
            GRADLE_WRAPPER_JAR_SHA256,
        )

        emulator_prepare = (
            ROOT / "scripts/prepare_android_emulator.sh"
        ).read_text(encoding="utf-8")
        sdl2_prepare = (ROOT / "scripts/prepare_android_sdl2.sh").read_text(
            encoding="utf-8"
        )
        for script in (emulator_prepare, sdl2_prepare):
            validation = script.index("scripts/android_patchset.py")
            stamp = script.index('printf \'%s\\n\' "$patch_set" > "$stamp"')
            self.assertLess(validation, stamp)

        self.assertIn(
            '"distributionSha256Sum=$GRADLE_DISTRIBUTION_SHA256"',
            emulator_prepare,
        )
        self.assertIn(
            '"$wrapper_jar_sha256" != "$GRADLE_WRAPPER_JAR_SHA256"',
            emulator_prepare,
        )

        series = (
            ROOT / "android/emulator/patches/series"
        ).read_text(encoding="utf-8").splitlines()
        self.assertEqual(
            series[-7:],
            [
                "0007-android-build-provenance.patch",
                "0008-android-sdl2-build-paths.patch",
                "0009-android-native-source-pins.patch",
                "0010-volatile-xbox-hdd.patch",
                "0011-android-volatile-runtime-files.patch",
                "0012-android-save-vault.patch",
                "0013-android-save-migration.patch",
            ],
        )
        provenance_patch = (
            ROOT
            / "android/emulator/patches/0007-android-build-provenance.patch"
        ).read_text(encoding="utf-8")
        self.assertIn(
            f"distributionSha256Sum={GRADLE_DISTRIBUTION_SHA256}",
            provenance_patch,
        )
        self.assertIn("android/app/gradle.lockfile", provenance_patch)
        self.assertIn(
            "android/gradle/verification-metadata.xml", provenance_patch
        )
        self.assertIn("LockMode.STRICT", provenance_patch)
        self.assertIn("resolveLockedDependencies", provenance_patch)

        platform_patch = (
            ROOT
            / "android/emulator/patches/0002-gdox-android-platform.patch"
        ).read_text(encoding="utf-8")
        native_revision_variables = {
            "TOMLPLUSPLUS_REVISION": "TOMLPLUSPLUS_GIT_REV",
            "VOLK_REVISION": "VOLK_GIT_REV",
            "GLSLANG_REVISION": "GLSLANG_GIT_REV",
            "SPIRV_REFLECT_REVISION": "SPIRV_REFLECT_GIT_REV",
            "VMA_REVISION": "VMA_GIT_REV",
        }
        for key, variable in native_revision_variables.items():
            self.assertIn(
                f'set({variable} "{dependencies[key]}")',
                platform_patch,
            )
        self.assertIn(
            f'set(GLIB_VERSION "{dependencies["GLIB_VERSION"]}")',
            platform_patch,
        )
        self.assertIn(
            'URL_HASH "SHA256='
            + dependencies["GLIB_SOURCE_SHA256"].upper()
            + '"',
            platform_patch,
        )

        source_pins_patch = (
            ROOT
            / "android/emulator/patches/0009-android-native-source-pins.patch"
        ).read_text(encoding="utf-8")
        self.assertIn('GIT_SUBMODULES ""', source_pins_patch)
        for key in (
            "GLIB_LIBFFI_REVISION",
            "GLIB_PROXYLIBINTL_REVISION",
        ):
            self.assertIn(dependencies[key], source_pins_patch)

        volatile_patch = (
            ROOT
            / "android/emulator/patches/0010-volatile-xbox-hdd.patch"
        ).read_text(encoding="utf-8")
        for required in (
            "block/xbox-volatile-hdd.c",
            "include/xemu-config.h",
            "scripts/xemu-version.sh",
            'test "${SOURCE_DATE_EPOCH+x}" = x',
            "SOURCE_DATE_EPOCH must be a non-negative integer",
            'date -u -d "@$SOURCE_DATE_EPOCH"',
            'date -u -r "$SOURCE_DATE_EPOCH"',
            "XBOX_VOLATILE_HDD_CACHE_START UINT64_C(0x00080000)",
            "XBOX_VOLATILE_HDD_CACHE_END   UINT64_C(0x8ca80000)",
            "XBOX_VOLATILE_HDD_MAX_PAGE_LIMIT 65536",
            "XBOX_VOLATILE_HDD_MAX_DIRTY_BYTES",
            "XBOX_VOLATILE_HDD_UNKNOWN_HOST_DIRTY_BYTES GiB",
            "qemu_get_host_physmem()",
            "return -ENOSPC;",
            "qemu_co_mutex_lock(&s->request_lock)",
            "bool volatile_hard_disk",
            "g_config.sys.volatile_hard_disk = true",
            "qemu_opt_set_bool(opts, BDRV_OPT_READ_ONLY, true",
            "--gdox-capabilities",
            r'\"schema\":3,\"runtime\":\"xemu\"',
            "full_hdd_ram_cow",
            "persistent_save_export",
            "source_projection_complete",
            "vault_merge_has_file_ancestor",
            "Xbox HDD image must not depend on a backing image",
            "Could not exclusively lock Xbox HDD for removal",
            "Multiple Xbox HDD removal quarantines are valid",
            "migration_cleanup_empty_quarantines",
            r'\"hdd-config-v1+E:\\\\UDATA+reviewed-E:\\\\TDATA\"',
            r'\"persistent_save_format\":\"logical-files-v2\"',
            "max_dirty_bytes",
            "*nperm = BLK_PERM_CONSISTENT_READ;",
        ):
            self.assertIn(required, volatile_patch)
        for forbidden in (
            ".is_filter",
            "volatile_cache_partitions",
            "bdrv_co_pwritev_part(bs->backing",
            "bdrv_co_pwrite_zeroes(bs->backing",
            "bdrv_co_pdiscard(bs->backing",
            "bdrv_co_flush(bs->backing",
            r'\"schema\":2,\"runtime\":\"xemu\"',
            r'\"persistent_save_scope\":\"E:\\\\UDATA\"',
            r'\"persistent_save_format\":\"logical-files-v1\"',
        ):
            self.assertNotIn(forbidden, volatile_patch)

        save_vault_patch = (
            ROOT
            / "android/emulator/patches/0012-android-save-vault.patch"
        ).read_text(encoding="utf-8")
        for required in (
            "ResolveSaveVault",
            "SDL_AndroidGetInternalStoragePath()",
            "CanonicalPath(internalPath, &internalRoot)",
            "IsWithinDirectory(internalRoot, canonical)",
            'kComponents[] = {"xemu", "saves", "v1"}',
            "O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC",
            "fchmod(directory, S_IRWXU)",
            "(st.st_mode & 0777) == S_IRWXU",
            "out.save_vault = ResolveSaveVault()",
            "static_assert(XBOX_SAVE_VAULT_CAPABILITY_SCHEMA == 3U)",
            "setenv(XBOX_SAVE_VAULT_ENVIRONMENT, setup.save_vault.c_str(), 1)",
            "Saved-game vault path is unavailable or unsafe",
        ):
            self.assertIn(required, save_vault_patch)
        for forbidden in (
            "SDL_AndroidGetExternalStoragePath",
            "xbox_hdd",
            "out.hdd",
            "--gdox-save-vault",
        ):
            self.assertNotIn(forbidden, save_vault_patch)
        self.assertLess(
            save_vault_patch.index(
                "setenv(XBOX_SAVE_VAULT_ENVIRONMENT, "
                "setup.save_vault.c_str(), 1)"
            ),
            save_vault_patch.index("if (!setup.config_path.empty())"),
        )

        runtime_files_patch = (
            ROOT
            / "android/emulator/patches/0011-android-volatile-runtime-files.patch"
        ).read_text(encoding="utf-8")
        for required in (
            'path + ".part"',
            "std::remove(temporaryPath.c_str())",
            "std::rename(temporaryPath.c_str(), path.c_str())",
            'HasException(env, "InputStream.read")',
            'HasException(env, "OutputStream.write")',
            'setenv("MESA_SHADER_CACHE_DISABLE", "1", 1)',
            'setenv("__GL_SHADER_DISK_CACHE", "0", 1)',
        ):
            self.assertIn(required, runtime_files_patch)

        migration_patch = (
            ROOT
            / "android/emulator/patches/0013-android-save-migration.patch"
        ).read_text(encoding="utf-8")
        for required in (
            'GDOX_CANONICAL_HDD "xbox_hdd.qcow2"',
            'GDOX_STAGED_HDD "xbox_hdd.clean.qcow2"',
            'GDOX_LEGACY_QCOW2 "hdd.qcow2"',
            'GDOX_LEGACY_RAW "hdd.img"',
            '"gdox_managed_hdd_role"',
            '"gdox_legacy_managed_hdd_role"',
            "expected_source_bytes = migration.legacy_identity.st_size",
            ".expected_source_sha256_valid = false",
            "xbox_save_migration_execute(&migration.request",
            "migration.proof.source_removed",
            "migration.proof.receipt_reused",
            "migration.proof.source_removal_safe",
            "migration.proof.source_projection_complete",
            "migration.proof.vault.unclassified_tdata_entries",
            "O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC",
            "O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC",
            "fstatat(directory_fd, name, status, AT_SYMLINK_NOFOLLOW)",
            "renameat(migration.directory_fd, GDOX_STAGED_HDD",
            "fsync(migration.directory_fd)",
            "xemu_android_display_wait_ready_timeout(100)",
            "SDL_AtomicGet(&launch_ctx.complete)",
            "gdoxShowStorageStatus",
        ):
            self.assertIn(required, migration_patch)
        for forbidden in (
            'GetPrefString(env, activity, "hddPath")',
            'GetPrefString(env, activity, "hddUri")',
            "xbox_save_migration_execute_and_remove",
        ):
            self.assertNotIn(forbidden, migration_patch)
        self.assertLess(
            migration_patch.index("create_promotion_marker(&error)"),
            migration_patch.index("xbox_save_migration_execute(&migration.request"),
        )

    def test_sdl2_build_paths_are_remapped(self) -> None:
        series = (
            ROOT / "android/emulator/patches/series"
        ).read_text(encoding="utf-8").splitlines()
        self.assertEqual(series[-6], "0008-android-sdl2-build-paths.patch")
        path_patch = (
            ROOT
            / "android/emulator/patches/0008-android-sdl2-build-paths.patch"
        ).read_text(encoding="utf-8")
        for map_type in ("file", "debug", "macro"):
            self.assertIn(
                f"-f{map_type}-prefix-map=${{SDL2_LOCAL_DIR}}=sdl2",
                path_patch,
            )
        self.assertEqual(path_patch.count("${SDL2_LOCAL_DIR}=sdl2"), 3)


if __name__ == "__main__":
    unittest.main()
