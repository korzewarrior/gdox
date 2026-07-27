package org.korze.gdox.android

import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbDeviceConnection
import android.hardware.usb.UsbManager
import android.os.Build

internal object GdoxDrive {
  const val vendorId = 0x0e8d
  const val productId = 0x1887

  fun find(manager: UsbManager): UsbDevice? =
    manager.deviceList.values.firstOrNull {
      it.vendorId == vendorId && it.productId == productId
    }

  fun fromIntent(intent: Intent): UsbDevice? =
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
      intent.getParcelableExtra(UsbManager.EXTRA_DEVICE, UsbDevice::class.java)
    } else {
      @Suppress("DEPRECATION")
      intent.getParcelableExtra(UsbManager.EXTRA_DEVICE)
    }

  fun matches(device: UsbDevice?): Boolean =
    device?.vendorId == vendorId && device.productId == productId
}

internal class GdoxUsbHost(
  private val context: Context,
  private val onDetached: (() -> Unit)? = null
) {
  private val manager =
    context.getSystemService(Context.USB_SERVICE) as UsbManager
  private var connection: UsbDeviceConnection? = null
  private var registered = false

  private val detachReceiver = object : BroadcastReceiver() {
    override fun onReceive(receivedContext: Context, intent: Intent) {
      if (intent.action != UsbManager.ACTION_USB_DEVICE_DETACHED
          || !GdoxDrive.matches(GdoxDrive.fromIntent(intent))) {
        return
      }
      /*
       * The native optical stack may still be completing a transfer. Signal
       * its orderly shutdown first; its owner closes this connection after
       * native I/O has drained.
       */
      onDetached?.invoke()
    }
  }

  fun start() {
    if (registered || onDetached == null) return
    val filter = IntentFilter(UsbManager.ACTION_USB_DEVICE_DETACHED)
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
      context.registerReceiver(
        detachReceiver,
        filter,
        Context.RECEIVER_EXPORTED
      )
    } else {
      @Suppress("DEPRECATION")
      context.registerReceiver(detachReceiver, filter)
    }
    registered = true
  }

  fun stop() {
    if (!registered) return
    context.unregisterReceiver(detachReceiver)
    registered = false
  }

  fun openFileDescriptor(): Int {
    close()
    val device = GdoxDrive.find(manager) ?: return -1
    if (!manager.hasPermission(device)) return -1
    val opened = manager.openDevice(device) ?: return -1
    connection = opened
    return opened.fileDescriptor
  }

  fun close() {
    connection?.close()
    connection = null
  }
}

internal class GdoxUsbPermission(
  private val context: Context,
  private val onChanged: () -> Unit
) {
  private val manager =
    context.getSystemService(Context.USB_SERVICE) as UsbManager
  private val action = "${context.packageName}.USB_PERMISSION"
  private var permissionRegistered = false
  private var deviceRegistered = false

  private val permissionReceiver = object : BroadcastReceiver() {
    override fun onReceive(receivedContext: Context, intent: Intent) {
      if (intent.action == action
          && GdoxDrive.matches(GdoxDrive.fromIntent(intent))) {
        onChanged()
      }
    }
  }

  private val deviceReceiver = object : BroadcastReceiver() {
    override fun onReceive(receivedContext: Context, intent: Intent) {
      if ((intent.action == UsbManager.ACTION_USB_DEVICE_ATTACHED
          || intent.action == UsbManager.ACTION_USB_DEVICE_DETACHED)
          && GdoxDrive.matches(GdoxDrive.fromIntent(intent))) {
        onChanged()
      }
    }
  }

  fun start() {
    if (permissionRegistered || deviceRegistered) return
    val permissionFilter = IntentFilter(action)
    val deviceFilter = IntentFilter().apply {
      addAction(UsbManager.ACTION_USB_DEVICE_ATTACHED)
      addAction(UsbManager.ACTION_USB_DEVICE_DETACHED)
    }
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
      context.registerReceiver(
        permissionReceiver,
        permissionFilter,
        Context.RECEIVER_NOT_EXPORTED
      )
      permissionRegistered = true
      context.registerReceiver(
        deviceReceiver,
        deviceFilter,
        Context.RECEIVER_EXPORTED
      )
      deviceRegistered = true
    } else {
      @Suppress("DEPRECATION")
      context.registerReceiver(permissionReceiver, permissionFilter)
      permissionRegistered = true
      @Suppress("DEPRECATION")
      context.registerReceiver(deviceReceiver, deviceFilter)
      deviceRegistered = true
    }
  }

  fun stop() {
    if (permissionRegistered) {
      context.unregisterReceiver(permissionReceiver)
      permissionRegistered = false
    }
    if (deviceRegistered) {
      context.unregisterReceiver(deviceReceiver)
      deviceRegistered = false
    }
  }

  fun request() {
    val device = GdoxDrive.find(manager) ?: return
    if (manager.hasPermission(device)) {
      onChanged()
      return
    }
    val intent = PendingIntent.getBroadcast(
      context,
      0,
      Intent(action).setPackage(context.packageName),
      PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
    )
    manager.requestPermission(device, intent)
  }

  fun snapshot(): GdoxUsbSnapshot {
    val device = GdoxDrive.find(manager)
    return GdoxUsbSnapshot(
      connected = device != null,
      permissionGranted = device != null && manager.hasPermission(device),
      productName = device?.productName,
      deviceId = device?.deviceId
    )
  }
}

internal data class GdoxUsbSnapshot(
  val connected: Boolean,
  val permissionGranted: Boolean,
  val productName: String?,
  val deviceId: Int?
)
