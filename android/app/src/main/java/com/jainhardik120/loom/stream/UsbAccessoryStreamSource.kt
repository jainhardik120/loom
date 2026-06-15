package com.jainhardik120.loom.stream

import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.hardware.usb.UsbAccessory
import android.hardware.usb.UsbManager
import android.os.ParcelFileDescriptor
import android.util.Log
import java.io.FileInputStream
import java.io.InputStream
import java.util.concurrent.atomic.AtomicBoolean

class UsbAccessoryStreamSource(context: Context) : StreamInputSource {
    override val name = "USB accessory"
    private val tag = "LoomUsbAccessory"
    private val appContext = context.applicationContext
    private val usbManager = appContext.getSystemService(Context.USB_SERVICE) as UsbManager
    private var descriptor: ParcelFileDescriptor? = null
    private var pendingPermissionKey: String? = null
    private var lastPermissionRequestMs = 0L

    override fun open(running: AtomicBoolean): InputStream? {
        val accessory = findLoomAccessory() ?: run {
            pendingPermissionKey = null
            lastPermissionRequestMs = 0L
            return null
        }
        val accessoryKey = accessory.key()
        if (!usbManager.hasPermission(accessory)) {
            requestPermissionOnce(accessory, accessoryKey)
            return null
        }

        pendingPermissionKey = null
        lastPermissionRequestMs = 0L
        descriptor = usbManager.openAccessory(accessory)
        if (descriptor == null) {
            Log.w(tag, "failed to open accessory ${accessory.model}")
            return null
        }

        Log.i(tag, "opened accessory ${accessory.manufacturer} ${accessory.model}")
        return FileInputStream(descriptor!!.fileDescriptor)
    }

    override fun close() {
        try {
            descriptor?.close()
        } catch (_: Exception) {
        }
        descriptor = null
    }

    private fun requestPermissionOnce(accessory: UsbAccessory, accessoryKey: String) {
        val now = System.currentTimeMillis()
        if (pendingPermissionKey == accessoryKey && now - lastPermissionRequestMs < PERMISSION_RETRY_MS) {
            return
        }

        pendingPermissionKey = accessoryKey
        lastPermissionRequestMs = now
        Log.i(tag, "requesting permission for ${accessory.manufacturer} ${accessory.model}")
        usbManager.requestPermission(accessory, permissionIntent())
    }

    private fun findLoomAccessory(): UsbAccessory? {
        return usbManager.accessoryList
            ?.firstOrNull { it.manufacturer == "Loom" && it.model == "Loom Display" }
    }

    private fun permissionIntent(): PendingIntent {
        val flags = PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        return PendingIntent.getBroadcast(
            appContext,
            0,
            Intent(ACTION_USB_PERMISSION).setPackage(appContext.packageName),
            flags
        )
    }

    private fun UsbAccessory.key(): String {
        return listOf(
            manufacturer.orEmpty(),
            model.orEmpty(),
            version.orEmpty(),
            serial.orEmpty()
        ).joinToString("|")
    }

    companion object {
        const val ACTION_USB_PERMISSION = "com.jainhardik120.loom.USB_PERMISSION"
        private const val PERMISSION_RETRY_MS = 30_000L
    }
}
