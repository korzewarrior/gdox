"""Android facade, secure-storage, and shared-source architecture checks."""

from __future__ import annotations

from .repository import Repository


def check_android(repository: Repository) -> list[str]:
    failures: list[str] = []
    root = "android/app/src/main/java/org/korze/gdox/android"
    storage = {
        name: repository.source(f"{root}/{name}")
        for name in (
            "GdoxCoreFiles.kt",
            "GdoxManagedHdd.kt",
            "GdoxSecureFiles.kt",
        )
    }
    for name, limit in (
        ("GdoxCoreFiles.kt", 240),
        ("GdoxManagedHdd.kt", 360),
        ("GdoxSecureFiles.kt", 220),
    ):
        if storage[name].line_count > limit:
            failures.append(f"{name} exceeds its focused module boundary")

    core_files = storage["GdoxCoreFiles.kt"].text
    for required in (
        "GdoxManagedHdd.resolve(context, ::preferences)",
        "GdoxManagedHdd.problem(context)",
        'GdoxSecureFiles.digest(file, "MD5")',
    ):
        if required not in core_files:
            failures.append("Android core-files facade bypasses a focused module")
            break
    for forbidden in (
        "android.system.",
        "java.nio.file.",
        "MessageDigest",
        "managedHddSha256",
        "managedHddPromotionMarkerName",
    ):
        if forbidden in core_files:
            failures.append(
                "Android core-files facade owns secure storage implementation"
            )
            break

    managed_hdd = storage["GdoxManagedHdd.kt"].text
    for required in (
        "GdoxSecureFiles.ensureOwnedDirectory(directory)",
        "GdoxSecureFiles.copyExclusive(input, temporary)",
        "GdoxSecureFiles.durableRename(temporary, destination)",
        "promotionAuthorized = validPromotionMarker(context)",
    ):
        if required not in managed_hdd:
            failures.append("Android managed-HDD lifecycle is incomplete")
            break
    for forbidden in (
        "preferencesName",
        "MCPX ROM",
        "Xbox BIOS",
        "formatSize(",
        "MessageDigest",
        "android.system.",
    ):
        if forbidden in managed_hdd:
            failures.append("Android managed-HDD module crosses its boundary")
            break

    secure_files = storage["GdoxSecureFiles.kt"].text
    for required in (
        "LinkOption.NOFOLLOW_LINKS",
        "OsConstants.O_EXCL",
        "OsConstants.S_ISDIR(status.st_mode)",
        "status.st_uid == Process.myUid()",
        "Os.fsync(output.fd)",
        "Os.rename(source.absolutePath, destination.absolutePath)",
        "Os.remove(file.absolutePath)",
    ):
        if required not in secure_files:
            failures.append("Android durable filesystem primitives are incomplete")
            break
    for forbidden in (
        "SharedPreferences",
        "managedHdd",
        "MCPX",
        "formatSize(",
    ):
        if forbidden in secure_files:
            failures.append("Android secure-files module crosses its boundary")
            break

    android_graph = repository.cmake.document("android/native/CMakeLists.txt")
    android_arguments = {
        argument
        for command in android_graph.commands
        for argument in command.arguments
    }
    for source_set in (
        "GDOX_LIVE_DISC_SOURCES",
        "GDOX_MT1887_OPTICAL_SOURCES",
    ):
        if f"${{{source_set}}}" not in android_arguments:
            failures.append(
                f"Android build does not consume the shared {source_set} set"
            )
    return failures
