package com.nanodev.nanodecompiler

import android.content.ContentProvider
import android.content.ContentValues
import android.database.Cursor
import android.database.MatrixCursor
import android.net.Uri
import android.os.ParcelFileDescriptor
import android.provider.OpenableColumns
import android.webkit.MimeTypeMap
import java.io.File
import java.io.FileNotFoundException

class NanoFileProvider : ContentProvider() {

    override fun onCreate(): Boolean = true

    override fun query(
        uri: Uri,
        projection: Array<out String>?,
        selection: String?,
        selectionArgs: Array<out String>?,
        sortOrder: String?
    ): Cursor {
        val file = getFileForUri(uri)
        val proj = projection ?: arrayOf(OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE)
        val cursor = MatrixCursor(proj, 1)
        val row = cursor.newRow()
        for (col in proj) {
            when (col) {
                OpenableColumns.DISPLAY_NAME -> row.add(file.name)
                OpenableColumns.SIZE -> row.add(file.length())
                else -> row.add(null)
            }
        }
        return cursor
    }

    override fun getType(uri: Uri): String {
        val file = getFileForUri(uri)
        val ext = file.extension
        if (ext.equals("zip", ignoreCase = true)) {
            return "application/zip"
        }
        if (ext.equals("apk", ignoreCase = true)) {
            return "application/vnd.android.package-archive"
        }
        val mime = MimeTypeMap.getSingleton().getMimeTypeFromExtension(ext.lowercase())
        return mime ?: "application/octet-stream"
    }

    override fun openFile(uri: Uri, mode: String): ParcelFileDescriptor? {
        val file = getFileForUri(uri)
        val fileMode = when (mode) {
            "r" -> ParcelFileDescriptor.MODE_READ_ONLY
            "w", "wt" -> ParcelFileDescriptor.MODE_WRITE_ONLY or ParcelFileDescriptor.MODE_CREATE or ParcelFileDescriptor.MODE_TRUNCATE
            "wa" -> ParcelFileDescriptor.MODE_WRITE_ONLY or ParcelFileDescriptor.MODE_CREATE or ParcelFileDescriptor.MODE_APPEND
            "rw" -> ParcelFileDescriptor.MODE_READ_WRITE or ParcelFileDescriptor.MODE_CREATE
            else -> throw IllegalArgumentException("Unsupported mode: $mode")
        }
        return ParcelFileDescriptor.open(file, fileMode)
    }

    override fun insert(uri: Uri, values: ContentValues?): Uri? = null
    override fun delete(uri: Uri, selection: String?, selectionArgs: Array<out String>?): Int = 0
    override fun update(uri: Uri, values: ContentValues?, selection: String?, selectionArgs: Array<out String>?): Int = 0

    private fun getFileForUri(uri: Uri): File {
        val context = context ?: throw IllegalStateException("Context is null")
        val path = uri.path ?: throw FileNotFoundException("Missing path: $uri")
        val cleanPath = if (path.startsWith("/")) path.substring(1) else path

        // Разрешаем доступ к файлам приложения (внутренним и внешним)
        val candidates = listOf(
            context.filesDir,
            context.cacheDir,
            context.getExternalFilesDir(null),
            context.getExternalFilesDir(android.os.Environment.DIRECTORY_DOWNLOADS)
        ).filterNotNull()

        for (base in candidates) {
            val f = File(base, cleanPath)
            if (f.exists()) return f
        }

        // Прямой абсолютный путь если существует
        val direct = File(path)
        if (direct.exists()) return direct

        throw FileNotFoundException("File not found for URI: $uri")
    }

    companion object {
        const val AUTHORITY = "com.nanodev.nanodecompiler.fileprovider"

        fun getUriForFile(context: android.content.Context, file: File): Uri {
            // Относительный путь от filesDir или getExternalFilesDir
            val extDir = context.getExternalFilesDir(null)
            val cacheDir = context.cacheDir
            val relPath = when {
                extDir != null && file.absolutePath.startsWith(extDir.absolutePath) -> {
                    file.absolutePath.removePrefix(extDir.absolutePath)
                }
                file.absolutePath.startsWith(context.filesDir.absolutePath) -> {
                    file.absolutePath.removePrefix(context.filesDir.absolutePath)
                }
                file.absolutePath.startsWith(cacheDir.absolutePath) -> {
                    file.absolutePath.removePrefix(cacheDir.absolutePath)
                }
                else -> file.absolutePath
            }
            val formatted = if (relPath.startsWith("/")) relPath.substring(1) else relPath
            return Uri.parse("content://$AUTHORITY/$formatted")
        }
    }
}
