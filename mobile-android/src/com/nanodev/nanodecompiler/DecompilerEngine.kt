package com.nanodev.nanodecompiler

import android.content.Context
import android.os.Build
import android.util.Log
import java.io.BufferedReader
import java.io.File
import java.io.FileOutputStream
import java.io.InputStreamReader
import java.nio.charset.StandardCharsets

data class DecompileResult(
    val ok: Boolean,
    val outDir: String,
    val elapsedSeconds: Double,
    val classesDecompiled: Int,
    val error: String? = null
)

object DecompilerEngine {
    private const val TAG = "DecompilerEngine"
    private var activeProcess: Process? = null

    private fun resolveEngineBinary(context: Context): File? {
        val nativeDir = File(context.applicationInfo.nativeLibraryDir)
        val nativeSo = File(nativeDir, "libnanodecompiler_cli.so")
        if (nativeSo.exists()) {
            nativeSo.setExecutable(true, false)
            return nativeSo
        }

        val binFile = File(context.filesDir, "NanoDecompilerCLI")
        try {
            if (!binFile.exists() || binFile.length() < 2000000L) {
                context.assets.open("bin/NanoDecompilerCLI").use { input ->
                    FileOutputStream(binFile).use { output ->
                        input.copyTo(output)
                    }
                }
            }
            binFile.setExecutable(true, false)
            binFile.setReadable(true, false)
            try {
                Runtime.getRuntime().exec(arrayOf("chmod", "755", binFile.absolutePath)).waitFor()
            } catch (_: Exception) {}
            return binFile
        } catch (e: Exception) {
            Log.e(TAG, "Failed to extract fallback NanoDecompilerCLI: ${e.message}")
        }

        return if (binFile.exists()) binFile else null
    }

    fun stop() {
        try {
            activeProcess?.let {
                it.destroy()
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                    it.destroyForcibly()
                }
            }
        } catch (e: Exception) {
            Log.w(TAG, "Error stopping process: ${e.message}")
        } finally {
            activeProcess = null
        }
    }

    fun getDecompiledBaseDir(context: Context): File {
        // Проверяем публичную папку Загрузки/NanoDecompiler (Environment.getExternalStoragePublicDirectory)
        try {
            val publicDownloads = android.os.Environment.getExternalStoragePublicDirectory(android.os.Environment.DIRECTORY_DOWNLOADS)
            if (publicDownloads != null) {
                val appDownloads = File(publicDownloads, "NanoDecompiler")
                if (!appDownloads.exists()) appDownloads.mkdirs()
                if (appDownloads.canWrite()) {
                    return appDownloads
                }
            }
        } catch (_: Exception) {}

        // Fallback: getExternalFilesDir(Environment.DIRECTORY_DOWNLOADS)
        val extDownloads = context.getExternalFilesDir(android.os.Environment.DIRECTORY_DOWNLOADS)
        if (extDownloads != null) {
            val appDownloads = File(extDownloads, "NanoDecompiler")
            if (!appDownloads.exists()) appDownloads.mkdirs()
            return appDownloads
        }

        // Fallback: внутреннее хранилище
        val internalDir = File(context.filesDir, "NanoDecompiler")
        if (!internalDir.exists()) internalDir.mkdirs()
        return internalDir
    }

    fun decompile(
        context: Context,
        inputFile: File,
        skipLegitimacy: Boolean = false,
        onStage: (String) -> Unit,
        onFileProcessing: (String) -> Unit,
        onLog: (String) -> Unit
    ): DecompileResult {
        val startTime = System.currentTimeMillis()
        val baseDecompiled = getDecompiledBaseDir(context)

        val safeName = inputFile.name.replace(Regex("[^a-zA-Z0-9._-]"), "_")
        val outDir = File(baseDecompiled, safeName)
        if (outDir.exists()) outDir.deleteRecursively()
        outDir.mkdirs()

        if (!inputFile.exists() || inputFile.length() == 0L) {
            return DecompileResult(false, "", 0.0, 0, "Файл не существует или пуст")
        }

        val engine = resolveEngineBinary(context)
        if (engine == null || !engine.exists()) {
            val errMsg = "Нативный C++ движок NanoDecompilerCLI не найден"
            onLog("[!] $errMsg")
            return DecompileResult(false, outDir.absolutePath, 0.0, 0, errMsg)
        }

        var classesCount = 0
        try {
            val cmdList = mutableListOf(
                engine.absolutePath,
                inputFile.absolutePath,
                outDir.absolutePath
            )
            if (skipLegitimacy) {
                cmdList.add("--no-legitimacy-check")
            }

            val pb = ProcessBuilder(cmdList)
            pb.directory(context.filesDir)
            pb.redirectErrorStream(true)

            val env = pb.environment()
            val nativeLibDir = context.applicationInfo.nativeLibraryDir
            env["LD_LIBRARY_PATH"] = nativeLibDir

            val process = pb.start()
            activeProcess = process

            val reader = BufferedReader(InputStreamReader(process.inputStream, StandardCharsets.UTF_8))
            var line: String?
            while (reader.readLine().also { line = it } != null) {
                val l = line ?: continue

                // Перехват служебных меток стадий движка C++
                if (l.startsWith("##ND_STAGE:") && l.endsWith("##")) {
                    val stage = l.removePrefix("##ND_STAGE:").removeSuffix("##").trim()
                    onStage(stage)
                    continue
                }

                // Перехват текущего обрабатываемого файла
                if (l.startsWith("##ND_FILE:") && l.endsWith("##")) {
                    val classFile = l.removePrefix("##ND_FILE:").removeSuffix("##").trim()
                    onFileProcessing(classFile)
                    continue
                }

                // Перехват машиночитаемого JSON-результата
                if (l.startsWith("##ND_RESULT:") && l.endsWith("##")) {
                    continue
                }

                onLog(l)
            }

            val exitCode = process.waitFor()
            activeProcess = null

            // Подсчет сгенерированных .java файлов
            outDir.walkTopDown().forEach {
                if (it.isFile && it.extension.equals("java", ignoreCase = true)) {
                    classesCount++
                }
            }

            if (classesCount == 0 && exitCode != 0) {
                return DecompileResult(false, outDir.absolutePath, 0.0, 0, "Движок завершился с кодом $exitCode")
            }

        } catch (e: Throwable) {
            activeProcess = null
            val err = "Процесс прерван: ${e.message}"
            onLog("[!] $err")
            return DecompileResult(false, outDir.absolutePath, 0.0, 0, err)
        }

        val elapsed = (System.currentTimeMillis() - startTime) / 1000.0
        return DecompileResult(
            ok = true,
            outDir = outDir.absolutePath,
            elapsedSeconds = elapsed,
            classesDecompiled = classesCount,
            error = null
        )
    }
}
