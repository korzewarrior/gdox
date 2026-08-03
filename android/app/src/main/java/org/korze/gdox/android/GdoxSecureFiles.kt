package org.korze.gdox.android

import android.os.Process
import android.system.ErrnoException
import android.system.Os
import android.system.OsConstants
import java.io.File
import java.io.FileDescriptor
import java.io.FileOutputStream
import java.io.IOException
import java.io.InputStream
import java.nio.file.Files
import java.nio.file.LinkOption
import java.security.MessageDigest

internal object GdoxSecureFiles {
  private const val ownerReadOnlyMode = 256
  private const val ownerReadWriteMode = 384

  fun pathExistsNoFollow(file: File): Boolean = try {
    Files.exists(file.toPath(), LinkOption.NOFOLLOW_LINKS)
  } catch (_: SecurityException) {
    true
  }

  fun regularFileNoFollow(file: File): Boolean = try {
    Files.isRegularFile(file.toPath(), LinkOption.NOFOLLOW_LINKS)
  } catch (_: SecurityException) {
    false
  }

  fun ensureOwnedDirectory(directory: File): Boolean {
    if (!pathExistsNoFollow(directory)) {
      try {
        Files.createDirectory(directory.toPath())
      } catch (_: IOException) {
        return false
      } catch (_: SecurityException) {
        return false
      }
    }
    var descriptor: FileDescriptor? = null
    return try {
      val openDescriptor = Os.open(
        directory.absolutePath,
        OsConstants.O_RDONLY or OsConstants.O_NOFOLLOW or
          OsConstants.O_CLOEXEC,
        0
      )
      descriptor = openDescriptor
      val status = Os.fstat(openDescriptor)
      OsConstants.S_ISDIR(status.st_mode) && status.st_uid == Process.myUid()
    } catch (_: ErrnoException) {
      false
    } finally {
      close(descriptor)
    }
  }

  fun copyExclusive(input: InputStream, destination: File): Boolean {
    var descriptor: FileDescriptor? = null
    return try {
      val openDescriptor = Os.open(
        destination.absolutePath,
        OsConstants.O_WRONLY or OsConstants.O_CREAT or OsConstants.O_EXCL or
          OsConstants.O_NOFOLLOW or OsConstants.O_CLOEXEC,
        ownerReadWriteMode
      )
      descriptor = openDescriptor
      FileOutputStream(openDescriptor).use { output ->
        input.copyTo(output)
        output.flush()
        Os.fsync(output.fd)
      }
      descriptor = null
      true
    } catch (_: IOException) {
      false
    } catch (_: SecurityException) {
      false
    } catch (_: ErrnoException) {
      false
    } finally {
      close(descriptor)
    }
  }

  fun makeReadOnly(file: File): Boolean {
    val directory = file.parentFile ?: return false
    if (!ensureOwnedDirectory(directory)) return false
    var descriptor: FileDescriptor? = null
    return try {
      val openDescriptor = Os.open(
        file.absolutePath,
        OsConstants.O_RDONLY or OsConstants.O_NOFOLLOW or
          OsConstants.O_CLOEXEC,
        0
      )
      descriptor = openDescriptor
      Os.fchmod(openDescriptor, ownerReadOnlyMode)
      Os.fsync(openDescriptor)
      true
    } catch (_: ErrnoException) {
      false
    } finally {
      close(descriptor)
    }
  }

  fun durableRename(source: File, destination: File): Boolean {
    val directory = source.parentFile ?: return false
    if (destination.parentFile != directory ||
      !ensureOwnedDirectory(directory) || !regularFileNoFollow(source) ||
      pathExistsNoFollow(destination)) {
      return false
    }
    var descriptor: FileDescriptor? = null
    return try {
      val openDescriptor = openOwnedDirectory(directory) ?: return false
      descriptor = openDescriptor
      Os.rename(source.absolutePath, destination.absolutePath)
      Os.fsync(openDescriptor)
      true
    } catch (_: ErrnoException) {
      false
    } finally {
      close(descriptor)
    }
  }

  fun durableDelete(file: File): Boolean {
    val directory = file.parentFile ?: return false
    if (!ensureOwnedDirectory(directory) || !regularFileNoFollow(file)) {
      return false
    }
    var directoryDescriptor: FileDescriptor? = null
    var fileDescriptor: FileDescriptor? = null
    return try {
      val openFile = Os.open(
        file.absolutePath,
        OsConstants.O_RDONLY or OsConstants.O_NOFOLLOW or
          OsConstants.O_CLOEXEC,
        0
      )
      fileDescriptor = openFile
      if (!OsConstants.S_ISREG(Os.fstat(openFile).st_mode)) return false
      val openDirectory = openOwnedDirectory(directory) ?: return false
      directoryDescriptor = openDirectory
      Os.remove(file.absolutePath)
      Os.fsync(openDirectory)
      true
    } catch (_: ErrnoException) {
      false
    } finally {
      close(directoryDescriptor)
      close(fileDescriptor)
    }
  }

  fun digest(file: File, algorithm: String): ByteArray? = try {
    val digest = MessageDigest.getInstance(algorithm)
    file.inputStream().use { input ->
      val buffer = ByteArray(DEFAULT_BUFFER_SIZE)
      while (true) {
        val read = input.read(buffer)
        if (read < 0) break
        digest.update(buffer, 0, read)
      }
    }
    digest.digest()
  } catch (_: IOException) {
    null
  } catch (_: SecurityException) {
    null
  }

  fun hex(bytes: ByteArray): String =
    bytes.joinToString("") { "%02x".format(it.toInt() and 0xff) }

  private fun openOwnedDirectory(directory: File): FileDescriptor? {
    var descriptor: FileDescriptor? = null
    return try {
      val openDescriptor = Os.open(
        directory.absolutePath,
        OsConstants.O_RDONLY or OsConstants.O_NOFOLLOW or
          OsConstants.O_CLOEXEC,
        0
      )
      descriptor = openDescriptor
      val status = Os.fstat(openDescriptor)
      if (!OsConstants.S_ISDIR(status.st_mode) ||
        status.st_uid != Process.myUid()) return null
      descriptor = null
      openDescriptor
    } catch (_: ErrnoException) {
      null
    } finally {
      close(descriptor)
    }
  }

  private fun close(descriptor: FileDescriptor?) {
    descriptor ?: return
    try {
      Os.close(descriptor)
    } catch (_: ErrnoException) {
    }
  }
}
