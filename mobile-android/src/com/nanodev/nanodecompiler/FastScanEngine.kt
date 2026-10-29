package com.nanodev.nanodecompiler

import java.io.BufferedReader
import java.io.File
import java.io.FileInputStream
import java.io.InputStreamReader
import java.util.zip.ZipEntry
import java.util.zip.ZipFile
import java.util.zip.ZipInputStream
import kotlin.math.ln
import kotlin.math.max

data class FastScanReport(
    val fileName: String,
    val sizeBytes: Long,
    val totalClasses: Int,
    val totalPackages: Int,
    val pluginName: String,
    val version: String,
    val author: String,
    val mainClass: String,
    val maxEntropy: Double,
    val threatsFound: Int,
    val threatLevel: String,
    val isNanoGuardProtected: Boolean,
    val isSingleClass: Boolean
)

object FastScanEngine {
    private const val LN_2 = 0.6931471805599453

    private val SUPPORTED_EXTENSIONS = listOf(".jar", ".class", ".zip", ".tar.gz", ".7z", ".rar", ".war")

    fun isSupportedFile(name: String): Boolean {
        val lower = name.lowercase()
        return SUPPORTED_EXTENSIONS.any { lower.endsWith(it) }
    }

    // Вычисление информационной энтропии Шеннона по байтам
    fun shannonEntropy(bytes: ByteArray): Double {
        if (bytes.isEmpty()) return 0.0
        val counts = IntArray(256)
        for (b in bytes) {
            counts[b.toInt() and 0xFF]++
        }
        val len = bytes.size.toDouble()
        var entropy = 0.0
        for (c in counts) {
            if (c > 0) {
                val p = c.toDouble() / len
                entropy -= p * (ln(p) / LN_2)
            }
        }
        return entropy
    }

    fun scan(file: File): FastScanReport {
        val fileName = file.name
        val sizeBytes = file.length()

        if (!isSupportedFile(fileName)) {
            throw IllegalArgumentException("Неподдерживаемый формат файла: $fileName. Разрешены только .jar, .class, .zip, .rar, .7z")
        }

        // 1. Одиночный .class файл
        if (fileName.endsWith(".class", ignoreCase = true)) {
            val bytes = try { file.readBytes() } catch (_: Exception) { ByteArray(0) }
            val entropy = shannonEntropy(bytes)
            val baseName = fileName.removeSuffix(".class")
            return FastScanReport(
                fileName = fileName,
                sizeBytes = sizeBytes,
                totalClasses = 1,
                totalPackages = 1,
                pluginName = baseName,
                version = "1.0.0",
                author = "nanodev1488",
                mainClass = baseName,
                maxEntropy = (entropy * 100).toInt() / 100.0,
                threatsFound = 0,
                threatLevel = "clean",
                isNanoGuardProtected = false,
                isSingleClass = true
            )
        }

        // 2. Сканирование архива (.jar, .zip)
        var totalClasses = 0
        val packages = HashSet<String>()
        var isNanoGuardProtected = false
        var maxEntropy = 0.0
        var threatsFound = 0
        var pluginName = fileName
        for (ext in SUPPORTED_EXTENSIONS) {
            if (pluginName.endsWith(ext, ignoreCase = true)) {
                pluginName = pluginName.dropLast(ext.length)
                break
            }
        }
        var version = "1.0.0"
        var author = "nanodev1488"
        var mainClass = ""

        try {
            val zip = ZipFile(file)
            val entries = zip.entries()
            while (entries.hasMoreElements()) {
                val entry = entries.nextElement()
                val name = entry.name
                val upper = name.uppercase()

                // Детекция защиты NanoGuard
                if (upper.contains("NANOGUARD.INF") || upper.contains("NANOGUARD.BIN") ||
                    upper.contains("META-INF/NANOGUARD") || upper.contains(".NANOGUARD") ||
                    upper.contains("LIBNANOGUARD") || upper.contains("NANOGUARD.DLL") ||
                    upper.contains("NANOGUARD.SO")
                ) {
                    isNanoGuardProtected = true
                }

                if (!entry.isDirectory) {
                    if (name.endsWith(".class", ignoreCase = true) && !name.contains("module-info")) {
                        totalClasses++
                        val slashIdx = name.lastIndexOf('/')
                        if (slashIdx != -1) {
                            packages.add(name.substring(0, slashIdx))
                        }

                        // Сэмплирование энтропии первых 50 классов
                        if (totalClasses <= 50) {
                            try {
                                zip.getInputStream(entry).use { stream ->
                                    val bytes = stream.readBytes()
                                    val ent = shannonEntropy(bytes)
                                    if (ent > maxEntropy) maxEntropy = ent

                                    val str = String(bytes, Charsets.ISO_8859_1)
                                    if (str.contains("discord.com/api/webhooks") ||
                                        str.contains("pastebin.com/raw")
                                    ) {
                                        threatsFound++
                                    }
                                }
                            } catch (_: Exception) {}
                        }
                    }

                    // Чтение plugin.yml
                    if (name.equals("plugin.yml", ignoreCase = true) || name.endsWith("/plugin.yml", ignoreCase = true)) {
                        try {
                            zip.getInputStream(entry).use { isStream ->
                                val reader = BufferedReader(InputStreamReader(isStream))
                                var line = reader.readLine()
                                while (line != null) {
                                    val trimmed = line.trim()
                                    if (trimmed.startsWith("name:", ignoreCase = true)) {
                                        pluginName = trimmed.substringAfter(":").trim().removeSurrounding("'").removeSurrounding("\"")
                                    } else if (trimmed.startsWith("version:", ignoreCase = true)) {
                                        version = trimmed.substringAfter(":").trim().removeSurrounding("'").removeSurrounding("\"")
                                    } else if (trimmed.startsWith("author:", ignoreCase = true)) {
                                        author = trimmed.substringAfter(":").trim().removeSurrounding("'").removeSurrounding("\"")
                                    } else if (trimmed.startsWith("main:", ignoreCase = true)) {
                                        mainClass = trimmed.substringAfter(":").trim().removeSurrounding("'").removeSurrounding("\"")
                                    }
                                    line = reader.readLine()
                                }
                            }
                        } catch (_: Exception) {}
                    }
                }
            }
            zip.close()
        } catch (e: Exception) {
            // Fallback поток
            try {
                FileInputStream(file).use { fis ->
                    val zis = ZipInputStream(fis)
                    var entry: ZipEntry? = zis.nextEntry
                    while (entry != null) {
                        val name = entry.name
                        if (!entry.isDirectory && name.endsWith(".class", ignoreCase = true)) {
                            totalClasses++
                            val slashIdx = name.lastIndexOf('/')
                            if (slashIdx != -1) packages.add(name.substring(0, slashIdx))
                        }
                        zis.closeEntry()
                        entry = zis.nextEntry
                    }
                }
            } catch (_: Exception) {}
        }

        val threatLevel = when {
            threatsFound > 0 -> "dangerous"
            maxEntropy > 7.3 -> "suspicious"
            else -> "clean"
        }

        return FastScanReport(
            fileName = fileName,
            sizeBytes = sizeBytes,
            totalClasses = totalClasses,
            totalPackages = if (packages.isEmpty() && totalClasses > 0) 1 else packages.size,
            pluginName = pluginName,
            version = version,
            author = author,
            mainClass = mainClass,
            maxEntropy = (maxEntropy * 100).toInt() / 100.0,
            threatsFound = threatsFound,
            threatLevel = threatLevel,
            isNanoGuardProtected = isNanoGuardProtected,
            isSingleClass = false
        )
    }
}
