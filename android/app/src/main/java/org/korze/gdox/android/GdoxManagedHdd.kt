package org.korze.gdox.android

import android.content.Context
import android.content.SharedPreferences
import java.io.File
import java.io.IOException

internal object GdoxManagedHdd {
  private const val managedHddName = "xbox_hdd.qcow2"
  private const val stagedManagedHddName = "xbox_hdd.clean.qcow2"
  private val legacyManagedHddNames = listOf("hdd.qcow2", "hdd.img")
  private const val managedHddRoleKey = "gdox_managed_hdd_role"
  private const val legacyManagedHddRoleKey = "gdox_legacy_managed_hdd_role"
  private const val managedHddPromotionMarkerName =
    "managed_hdd_promotion_v1.complete"
  private const val managedHddPromotionMarkerContents =
    "gdox-managed-hdd-promotion-v1\n"
  private val managedHddPromotionMarkerBytes =
    managedHddPromotionMarkerContents.toByteArray(Charsets.UTF_8)
  private const val managedHddSize = 1638400L
  private const val managedHddSha256 =
    "00d7df7a2bc235f8801764f00b7f40e194d1e392f7a9619d6b2396c89770f6dd"

  enum class Action {
    NONE,
    INSTALL_CANONICAL,
    INSTALL_STAGED,
    PROMOTE_STAGED,
    REMOVE_REDUNDANT_STAGED
  }

  data class Plan(
    val active: File? = null,
    val legacy: File? = null,
    val action: Action = Action.NONE,
    val problem: String? = null
  )

  fun resolve(
    context: Context,
    preferences: (Context) -> SharedPreferences
  ): File? {
    val root = context.getExternalFilesDir(null) ?: context.filesDir
    val directory = File(root, "gdox")
    if (!GdoxSecureFiles.ensureOwnedDirectory(directory)) {
      recordPlan(context, null, preferences)
      return null
    }
    repeat(3) {
      val plan = plan(
        directory,
        promotionAuthorized = validPromotionMarker(context)
      )
      if (plan.problem != null) {
        recordPlan(context, null, preferences)
        return null
      }
      when (plan.action) {
        Action.INSTALL_CANONICAL ->
          if (!install(context, File(directory, managedHddName))) {
            recordPlan(context, null, preferences)
            return null
          }
        Action.INSTALL_STAGED ->
          if (!install(context, File(directory, stagedManagedHddName))) {
            recordPlan(context, null, preferences)
            return null
          }
        Action.PROMOTE_STAGED -> {
          val staged = File(directory, stagedManagedHddName)
          val canonical = File(directory, managedHddName)
          if (!valid(staged) ||
            GdoxSecureFiles.pathExistsNoFollow(canonical) ||
            !GdoxSecureFiles.durableRename(staged, canonical)) {
            recordPlan(context, null, preferences)
            return null
          }
          if (!GdoxSecureFiles.makeReadOnly(canonical) ||
            !clearPromotionMarker(context)) {
            recordPlan(context, null, preferences)
            return null
          }
        }
        Action.REMOVE_REDUNDANT_STAGED -> {
          val staged = File(directory, stagedManagedHddName)
          if (valid(staged) && !GdoxSecureFiles.durableDelete(staged)) {
            recordPlan(context, null, preferences)
            return null
          }
          val ready = plan.copy(action = Action.NONE)
          if (ready.active == null ||
            !GdoxSecureFiles.makeReadOnly(ready.active)) return null
          if (!recordPlan(context, ready, preferences)) return null
          if (ready.legacy == null) {
            clearLegacyHddPreferences(context, preferences)
          }
          if (!clearPromotionMarker(context)) return null
          return ready.active
        }
        Action.NONE -> {
          if (plan.active == null) {
            recordPlan(context, null, preferences)
            return null
          }
          if (!GdoxSecureFiles.makeReadOnly(plan.active) ||
            !recordPlan(context, plan, preferences)) return null
          if (plan.active.name == stagedManagedHddName &&
            plan.legacy?.name == managedHddName &&
            !clearPromotionMarker(context)) return null
          if (plan.legacy == null) {
            clearLegacyHddPreferences(context, preferences)
          }
          if (valid(File(directory, managedHddName)) &&
            !clearPromotionMarker(context)) return null
          return plan.active
        }
      }
    }
    recordPlan(context, null, preferences)
    return null
  }

  fun plan(
    directory: File,
    present: (File) -> Boolean = {
      GdoxSecureFiles.pathExistsNoFollow(it)
    },
    regular: (File) -> Boolean = {
      GdoxSecureFiles.regularFileNoFollow(it)
    },
    clean: (File) -> Boolean = { valid(it) },
    promotionAuthorized: Boolean = false
  ): Plan {
    val canonical = File(directory, managedHddName)
    val staged = File(directory, stagedManagedHddName)
    val oldCandidates = legacyManagedHddNames
      .map { File(directory, it) }
      .filter(present)
    val canonicalPresent = present(canonical)
    val stagedPresent = present(staged)
    if (oldCandidates.size > 1) return unsafePlan()
    val old = oldCandidates.singleOrNull()

    if (old != null && !regular(old)) return unsafePlan()
    if (canonicalPresent && clean(canonical)) {
      if (stagedPresent && !clean(staged)) return unsafePlan()
      return Plan(
        active = canonical,
        legacy = old,
        action = if (stagedPresent) {
          Action.REMOVE_REDUNDANT_STAGED
        } else {
          Action.NONE
        }
      )
    }
    if (canonicalPresent) {
      if (!regular(canonical) || old != null) return unsafePlan()
      if (!stagedPresent) return Plan(action = Action.INSTALL_STAGED)
      if (!clean(staged)) return unsafePlan()
      return Plan(active = staged, legacy = canonical)
    }
    if (stagedPresent) {
      if (!clean(staged) || !promotionAuthorized) return unsafePlan()
      return Plan(action = Action.PROMOTE_STAGED)
    }
    return Plan(action = Action.INSTALL_CANONICAL)
  }

  fun valid(file: File): Boolean {
    if (!GdoxSecureFiles.regularFileNoFollow(file) || !file.canRead() ||
      file.length() != managedHddSize) {
      return false
    }
    val digest = GdoxSecureFiles.digest(file, "SHA-256") ?: return false
    return GdoxSecureFiles.hex(digest) == managedHddSha256
  }

  fun problem(context: Context): String? {
    val root = context.getExternalFilesDir(null) ?: context.filesDir
    val directory = File(root, "gdox")
    if (!GdoxSecureFiles.ensureOwnedDirectory(directory)) {
      return unsafePlan().problem
    }
    return plan(
      directory,
      promotionAuthorized = validPromotionMarker(context)
    ).problem
  }

  private fun unsafePlan() = Plan(
    problem = "GDOX preserved managed hard-disk files it could not migrate " +
      "safely. Back up the app's data before changing those files."
  )

  private fun install(context: Context, destination: File): Boolean {
    val directory = destination.parentFile ?: return false
    val temporary = File(directory, "${destination.name}.part")
    if (!GdoxSecureFiles.ensureOwnedDirectory(directory) ||
      GdoxSecureFiles.pathExistsNoFollow(destination)) {
      return false
    }
    if (GdoxSecureFiles.pathExistsNoFollow(temporary)) {
      if (!GdoxSecureFiles.regularFileNoFollow(temporary)) return false
      if (valid(temporary)) {
        return GdoxSecureFiles.durableRename(temporary, destination) &&
          GdoxSecureFiles.makeReadOnly(destination)
      }
      if (!GdoxSecureFiles.durableDelete(temporary)) return false
    }

    val copied = try {
      context.assets.open(managedHddName).use { input ->
        GdoxSecureFiles.copyExclusive(input, temporary)
      }
    } catch (_: IOException) {
      false
    } catch (_: SecurityException) {
      false
    }
    if (!copied || !valid(temporary) ||
      GdoxSecureFiles.pathExistsNoFollow(destination) ||
      !GdoxSecureFiles.durableRename(temporary, destination) ||
      !GdoxSecureFiles.makeReadOnly(destination)) {
      if (GdoxSecureFiles.regularFileNoFollow(temporary)) {
        GdoxSecureFiles.durableDelete(temporary)
      }
      return false
    }
    return true
  }

  private fun recordPlan(
    context: Context,
    plan: Plan?,
    preferences: (Context) -> SharedPreferences
  ): Boolean {
    val editor = preferences(context).edit()
    when (plan?.active?.name) {
      managedHddName -> editor.putString(managedHddRoleKey, "canonical")
      stagedManagedHddName -> editor.putString(managedHddRoleKey, "staged")
      null -> editor.remove(managedHddRoleKey)
      else -> return false
    }
    when (plan?.legacy?.name) {
      "hdd.img" -> editor.putString(legacyManagedHddRoleKey, "hdd-img")
      "hdd.qcow2" ->
        editor.putString(legacyManagedHddRoleKey, "hdd-qcow2")
      managedHddName -> editor.putString(
        legacyManagedHddRoleKey,
        "canonical"
      )
      null -> editor.remove(legacyManagedHddRoleKey)
      else -> return false
    }
    return editor.commit()
  }

  private fun promotionMarker(context: Context): File = File(
    File(File(context.filesDir, "xemu"), "migration"),
    managedHddPromotionMarkerName
  )

  private fun validPromotionMarker(context: Context): Boolean {
    val marker = promotionMarker(context)
    if (!GdoxSecureFiles.regularFileNoFollow(marker) ||
      marker.length() != managedHddPromotionMarkerBytes.size.toLong()) {
      return false
    }
    return try {
      marker.readBytes().contentEquals(managedHddPromotionMarkerBytes)
    } catch (_: IOException) {
      false
    } catch (_: SecurityException) {
      false
    }
  }

  private fun clearPromotionMarker(context: Context): Boolean {
    val marker = promotionMarker(context)
    if (!GdoxSecureFiles.pathExistsNoFollow(marker)) return true
    return validPromotionMarker(context) &&
      GdoxSecureFiles.durableDelete(marker)
  }

  private fun clearLegacyHddPreferences(
    context: Context,
    preferences: (Context) -> SharedPreferences
  ) {
    preferences(context).edit()
      .remove("hddPath")
      .remove("hddUri")
      .apply()
  }
}
