package com.nanodev.nanodecompiler

import android.animation.ObjectAnimator
import android.animation.ValueAnimator
import android.app.Activity
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.graphics.*
import android.graphics.drawable.BitmapDrawable
import android.graphics.drawable.GradientDrawable
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Vibrator
import android.provider.OpenableColumns
import android.text.Editable
import android.text.Spannable
import android.text.SpannableStringBuilder
import android.text.TextWatcher
import android.text.style.ForegroundColorSpan
import android.text.style.StyleSpan
import android.util.TypedValue
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.view.Window
import android.view.WindowManager
import android.view.animation.AccelerateDecelerateInterpolator
import android.widget.*
import java.io.File
import java.io.FileOutputStream
import java.nio.charset.StandardCharsets
import java.util.concurrent.Executors
import java.util.regex.Pattern

class MainActivity : Activity() {

    private val bgExecutor = Executors.newSingleThreadExecutor()
    private var vibrator: Vibrator? = null

    // Liquid Glass & Material Design 3 Цветовая Палитра
    private val COLOR_BG = Color.parseColor("#05080C")
    private val COLOR_SURFACE = Color.parseColor("#0C121A")
    private val COLOR_SURFACE_GLASS = Color.parseColor("#131D28")
    private val COLOR_CARD_GLASS = Color.parseColor("#15202D")
    private val COLOR_BORDER = Color.parseColor("#223447")
    private val COLOR_BORDER_GLOW = Color.parseColor("#344E6B")
    private val COLOR_ACID = Color.parseColor("#9BF246")
    private val COLOR_ACID_DIM = Color.parseColor("#203014")
    private val COLOR_CYAN = Color.parseColor("#29B6F6")
    private val COLOR_TEXT = Color.parseColor("#EAF0EA")
    private val COLOR_TEXT_MUTED = Color.parseColor("#8E9DAA")
    private val COLOR_DANGER = Color.parseColor("#FF5370")

    // Цвета подсветки синтаксиса Java
    private val COLOR_SYNTAX_KEYWORD = Color.parseColor("#C792EA") // Пурпурный
    private val COLOR_SYNTAX_STRING = Color.parseColor("#C3E88D")  // Зеленый
    private val COLOR_SYNTAX_ANNOTATION = Color.parseColor("#FFCB6B") // Желтый/Охра
    private val COLOR_SYNTAX_COMMENT = Color.parseColor("#676E85") // Приглушенный серый
    private val COLOR_SYNTAX_NUMBER = Color.parseColor("#F78C6C")  // Оранжевый
    private val COLOR_SYNTAX_TYPE = Color.parseColor("#82AAFF")    // Голубой

    private val queueFiles = ArrayList<File>()
    private var activeReport: FastScanReport? = null
    private var activeFile: File? = null
    private var activeOutDir: File? = null
    private val decompiledFilesList = ArrayList<File>()
    private var isDecompiling = false

    private fun resetDecompileButton() {
        isDecompiling = false
        btnStartDecompile.text = "  Запустить декомпиляцию"
        btnStartDecompile.setTextColor(Color.BLACK)
        btnStartDecompile.setCompoundDrawablesWithIntrinsicBounds(createGoogleIcon("play", 16, Color.BLACK), null, null, null)
        btnStartDecompile.background = liquidGlassDrawable(COLOR_ACID, COLOR_ACID, 12f)
        btnStartDecompile.isEnabled = (activeFile != null)
        btnStartDecompile.alpha = if (activeFile != null) 1.0f else 0.4f
    }

    private fun stopDecompilation() {
        DecompilerEngine.stop()
        isDecompiling = false
        pbDecompile.visibility = View.GONE
        tvStatus.text = "остановлено"
        statusDot.background = liquidGlassDrawable(COLOR_DANGER, COLOR_DANGER, 4f)
        resetDecompileButton()
        vibrate(80)
        Toast.makeText(this, "Декомпиляция остановлена", Toast.LENGTH_SHORT).show()
    }

    // Настройки приложения (ПК 1:1)
    private var optDeobfuscateStrings = true
    private var optLegitimacyCheck = true
    private var optVulnerabilityScan = true
    private var optRestoreControlFlow = true
    private var optVibration = true
    private var optSyntaxHighlight = true
    private var optEditorFontSize = 11.5f
    private var optWordWrap = false

    private lateinit var contentContainer: FrameLayout
    private lateinit var tabHomeView: View
    private lateinit var tabQueueView: View
    private lateinit var tabEditorView: View
    private lateinit var tabTerminalView: View
    private lateinit var tabSettingsView: View

    private lateinit var tvStatus: TextView
    private lateinit var statusDot: View
    private lateinit var fileSelectedCard: LinearLayout
    private lateinit var tvFileTitle: TextView
    private lateinit var tvFileMeta: TextView
    private lateinit var btnStartDecompile: Button
    private lateinit var pbDecompile: ProgressBar
    private lateinit var queueContainer: LinearLayout

    // Полноценный Редактор кода
    private lateinit var sourceFilesContainer: LinearLayout
    private lateinit var etClassFilter: EditText
    private lateinit var tvCodeViewer: TextView
    private lateinit var tvLineNumbers: TextView
    private lateinit var tvCodePath: TextView
    private lateinit var tvCodeFooterMeta: TextView
    private lateinit var btnCopyCode: Button
    private lateinit var btnShareZip: Button
    private lateinit var btnToggleWrap: Button
    private var currentActiveFile: File? = null

    private lateinit var tvTerminal: TextView
    private lateinit var svTerminal: ScrollView
    private lateinit var dropCard: LinearLayout
    private lateinit var bottomNav: LinearLayout
    private val navButtons = ArrayList<Button>()

    private var currentTab = 0

    // Google Material Icons Generator (Рендерит векторные иконки Material Icons в высоком качестве)
    private fun createGoogleIcon(iconType: String, sizeDp: Int, color: Int): BitmapDrawable {
        val px = dp(sizeDp)
        val bitmap = Bitmap.createBitmap(px, px, Bitmap.Config.ARGB_8888)
        val canvas = Canvas(bitmap)
        val paint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
            this.color = color
            style = Paint.Style.STROKE
            strokeWidth = TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, 1.8f, resources.displayMetrics)
            strokeCap = Paint.Cap.ROUND
            strokeJoin = Paint.Join.ROUND
        }
        val fillPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
            this.color = color
            style = Paint.Style.FILL
        }

        val fSize = px.toFloat()
        when (iconType) {
            "folder" -> { // Google Icons: folder_open
                val p = Path().apply {
                    moveTo(fSize * 0.15f, fSize * 0.35f)
                    lineTo(fSize * 0.40f, fSize * 0.35f)
                    lineTo(fSize * 0.50f, fSize * 0.45f)
                    lineTo(fSize * 0.85f, fSize * 0.45f)
                    lineTo(fSize * 0.85f, fSize * 0.75f)
                    lineTo(fSize * 0.15f, fSize * 0.75f)
                    close()
                }
                canvas.drawPath(p, paint)
            }
            "play" -> { // Google Icons: play_arrow
                val p = Path().apply {
                    moveTo(fSize * 0.34f, fSize * 0.24f)
                    lineTo(fSize * 0.76f, fSize * 0.50f)
                    lineTo(fSize * 0.34f, fSize * 0.76f)
                    close()
                }
                canvas.drawPath(p, fillPaint)
            }
            "stop" -> { // Google Icons: stop square (⏹)
                val r = RectF(fSize * 0.28f, fSize * 0.28f, fSize * 0.72f, fSize * 0.72f)
                canvas.drawRoundRect(r, fSize * 0.08f, fSize * 0.08f, fillPaint)
            }
            "settings" -> { // Google Icons: settings gear
                canvas.drawCircle(fSize * 0.5f, fSize * 0.5f, fSize * 0.22f, paint)
                for (i in 0 until 8) {
                    val angle = (i * Math.PI / 4).toFloat()
                    val x1 = fSize * 0.5f + (fSize * 0.30f * Math.cos(angle.toDouble())).toFloat()
                    val y1 = fSize * 0.5f + (fSize * 0.30f * Math.sin(angle.toDouble())).toFloat()
                    val x2 = fSize * 0.5f + (fSize * 0.42f * Math.cos(angle.toDouble())).toFloat()
                    val y2 = fSize * 0.5f + (fSize * 0.42f * Math.sin(angle.toDouble())).toFloat()
                    canvas.drawLine(x1, y1, x2, y2, paint)
                }
            }
            "terminal" -> { // Google Icons: terminal prompt (>_)
                val p = Path().apply {
                    moveTo(fSize * 0.22f, fSize * 0.32f)
                    lineTo(fSize * 0.46f, fSize * 0.50f)
                    lineTo(fSize * 0.22f, fSize * 0.68f)
                }
                canvas.drawPath(p, paint)
                canvas.drawLine(fSize * 0.54f, fSize * 0.68f, fSize * 0.78f, fSize * 0.68f, paint)
            }
            "code" -> { // Google Icons: code (< / >)
                val p1 = Path().apply {
                    moveTo(fSize * 0.35f, fSize * 0.35f)
                    lineTo(fSize * 0.20f, fSize * 0.50f)
                    lineTo(fSize * 0.35f, fSize * 0.65f)
                }
                canvas.drawPath(p1, paint)
                val p2 = Path().apply {
                    moveTo(fSize * 0.65f, fSize * 0.35f)
                    lineTo(fSize * 0.80f, fSize * 0.50f)
                    lineTo(fSize * 0.65f, fSize * 0.65f)
                }
                canvas.drawPath(p2, paint)
                canvas.drawLine(fSize * 0.55f, fSize * 0.28f, fSize * 0.45f, fSize * 0.72f, paint)
            }
            "queue" -> { // Google Icons: layers / list
                canvas.drawLine(fSize * 0.2f, fSize * 0.32f, fSize * 0.8f, fSize * 0.32f, paint)
                canvas.drawLine(fSize * 0.2f, fSize * 0.50f, fSize * 0.8f, fSize * 0.50f, paint)
                canvas.drawLine(fSize * 0.2f, fSize * 0.68f, fSize * 0.8f, fSize * 0.68f, paint)
            }
            "copy" -> { // Google Icons: content_copy
                val p1 = Path().apply {
                    moveTo(fSize * 0.35f, fSize * 0.20f)
                    lineTo(fSize * 0.80f, fSize * 0.20f)
                    lineTo(fSize * 0.80f, fSize * 0.65f)
                }
                canvas.drawPath(p1, paint)
                val p2 = Path().apply {
                    moveTo(fSize * 0.20f, fSize * 0.35f)
                    lineTo(fSize * 0.65f, fSize * 0.35f)
                    lineTo(fSize * 0.65f, fSize * 0.80f)
                    lineTo(fSize * 0.20f, fSize * 0.80f)
                    close()
                }
                canvas.drawPath(p2, paint)
            }
            "search" -> { // Google Icons: search
                canvas.drawCircle(fSize * 0.42f, fSize * 0.42f, fSize * 0.24f, paint)
                canvas.drawLine(fSize * 0.60f, fSize * 0.60f, fSize * 0.82f, fSize * 0.82f, paint)
            }
            "delete" -> { // Google Icons: close
                canvas.drawLine(fSize * 0.28f, fSize * 0.28f, fSize * 0.72f, fSize * 0.72f, paint)
                canvas.drawLine(fSize * 0.72f, fSize * 0.28f, fSize * 0.28f, fSize * 0.72f, paint)
            }
            "share" -> { // Google Icons: share (nodes and links)
                val r = fSize * 0.12f
                val c1x = fSize * 0.75f; val c1y = fSize * 0.28f
                val c2x = fSize * 0.25f; val c2y = fSize * 0.50f
                val c3x = fSize * 0.75f; val c3y = fSize * 0.72f
                canvas.drawLine(c2x, c2y, c1x, c1y, paint)
                canvas.drawLine(c2x, c2y, c3x, c3y, paint)
                val fillPaint = Paint(paint).apply { style = Paint.Style.FILL }
                canvas.drawCircle(c1x, c1y, r, fillPaint)
                canvas.drawCircle(c2x, c2y, r, fillPaint)
                canvas.drawCircle(c3x, c3y, r, fillPaint)
            }
            "app_logo" -> { // PC Logo (Terminal + Acid Chip)
                val p = Path().apply {
                    moveTo(fSize * 0.24f, fSize * 0.34f)
                    lineTo(fSize * 0.46f, fSize * 0.50f)
                    lineTo(fSize * 0.24f, fSize * 0.66f)
                }
                canvas.drawPath(p, paint)
                canvas.drawLine(fSize * 0.52f, fSize * 0.66f, fSize * 0.76f, fSize * 0.66f, paint)
            }
        }
        return BitmapDrawable(resources, bitmap)
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        requestWindowFeature(Window.FEATURE_NO_TITLE)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
            window.addFlags(WindowManager.LayoutParams.FLAG_DRAWS_SYSTEM_BAR_BACKGROUNDS)
            window.statusBarColor = COLOR_BG
            window.navigationBarColor = COLOR_BG
        }

        vibrator = getSystemService(Context.VIBRATOR_SERVICE) as? Vibrator

        val root = FrameLayout(this).apply {
            setBackgroundColor(COLOR_BG)
            layoutParams = ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT)
        }

        // Ambient Liquid Glass Glow Orbs (Неоновые пятна света на фоне, как в PC версии!)
        val ambientCanvas = View(this).apply {
            layoutParams = FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT)
            background = object : android.graphics.drawable.Drawable() {
                override fun draw(canvas: Canvas) {
                    val w = bounds.width().toFloat()
                    val h = bounds.height().toFloat()

                    // Верхний зеленый неоновый шар
                    val greenPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
                        shader = RadialGradient(w * 0.85f, h * 0.12f, w * 0.45f,
                            Color.argb(40, 155, 242, 70), Color.TRANSPARENT, Shader.TileMode.CLAMP)
                    }
                    canvas.drawCircle(w * 0.85f, h * 0.12f, w * 0.45f, greenPaint)

                    // Нижний циан-пурпурный неоновый шар
                    val cyanPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
                        shader = RadialGradient(w * 0.15f, h * 0.80f, w * 0.55f,
                            Color.argb(30, 41, 182, 246), Color.TRANSPARENT, Shader.TileMode.CLAMP)
                    }
                    canvas.drawCircle(w * 0.15f, h * 0.80f, w * 0.55f, cyanPaint)
                }
                override fun setAlpha(alpha: Int) {}
                override fun setColorFilter(colorFilter: ColorFilter?) {}
                override fun getOpacity(): Int = PixelFormat.TRANSLUCENT
            }
        }
        root.addView(ambientCanvas)

        val mainLayout = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            layoutParams = FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT)
        }
        root.addView(mainLayout)

        // 1. Верхний бар
        val topBar = createTopBar()
        mainLayout.addView(topBar)

        // 2. Центральный контейнер 5 вкладок
        contentContainer = FrameLayout(this).apply {
            layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1.0f)
        }
        mainLayout.addView(contentContainer)

        // 3. Создаем вьюхи всех 5 вкладок (Главная, Архивы, Редактор, Терминал, Настройки)
        tabHomeView = createHomeTab()
        tabQueueView = createQueueTab()
        tabEditorView = createEditorTab()
        tabTerminalView = createTerminalTab()
        tabSettingsView = createSettingsTab()

        contentContainer.addView(tabHomeView)
        contentContainer.addView(tabQueueView)
        contentContainer.addView(tabEditorView)
        contentContainer.addView(tabTerminalView)
        contentContainer.addView(tabSettingsView)

        // 4. Нижняя навигационная панель (5 MD3 вкладок с Google Icons)
        bottomNav = createBottomNav()
        mainLayout.addView(bottomNav)

        setContentView(root)
        switchTab(0)
    }

    private fun dp(v: Int): Int {
        return TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v.toFloat(), resources.displayMetrics).toInt()
    }

    private fun liquidGlassDrawable(bgColor: Int, borderColor: Int, radiusDp: Float): GradientDrawable {
        return GradientDrawable().apply {
            shape = GradientDrawable.RECTANGLE
            cornerRadius = TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, radiusDp, resources.displayMetrics)
            setColor(bgColor)
            setStroke(dp(1), borderColor)
        }
    }

    private fun createTopBar(): View {
        return LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            setPadding(dp(14), dp(8), dp(14), dp(8))
            setBackgroundColor(COLOR_SURFACE)

            val ivLogo = ImageView(this@MainActivity).apply {
                setImageDrawable(createGoogleIcon("app_logo", 20, COLOR_ACID))
                background = liquidGlassDrawable(COLOR_ACID_DIM, COLOR_ACID, 8f)
                setPadding(dp(4), dp(4), dp(4), dp(4))
                layoutParams = LinearLayout.LayoutParams(dp(28), dp(28))
            }
            addView(ivLogo)

            val titleLayout = LinearLayout(this@MainActivity).apply {
                orientation = LinearLayout.HORIZONTAL
                gravity = Gravity.CENTER_VERTICAL
                val lp = LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1.0f)
                lp.marginStart = dp(8)
                layoutParams = lp
            }

            val tvTitle = TextView(this@MainActivity).apply {
                text = "NanoDecompiler"
                setTextColor(COLOR_TEXT)
                setTextSize(TypedValue.COMPLEX_UNIT_SP, 15f)
                typeface = Typeface.DEFAULT_BOLD
            }
            titleLayout.addView(tvTitle)

            val tvBadge = TextView(this@MainActivity).apply {
                text = "v2.0.3"
                setTextColor(COLOR_ACID)
                setTextSize(TypedValue.COMPLEX_UNIT_SP, 10f)
                background = liquidGlassDrawable(Color.argb(40, 155, 242, 70), COLOR_ACID, 6f)
                setPadding(dp(4), dp(1), dp(4), dp(1))
                val lp = LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT)
                lp.marginStart = dp(6)
                layoutParams = lp
            }
            titleLayout.addView(tvBadge)
            addView(titleLayout)

            val statusChip = LinearLayout(this@MainActivity).apply {
                orientation = LinearLayout.HORIZONTAL
                gravity = Gravity.CENTER_VERTICAL
                background = liquidGlassDrawable(COLOR_SURFACE_GLASS, COLOR_BORDER, 14f)
                setPadding(dp(8), dp(4), dp(8), dp(4))
            }

            statusDot = View(this@MainActivity).apply {
                background = liquidGlassDrawable(COLOR_ACID, COLOR_ACID, 4f)
                val lp = LinearLayout.LayoutParams(dp(6), dp(6))
                lp.marginEnd = dp(6)
                layoutParams = lp
            }
            statusChip.addView(statusDot)

            tvStatus = TextView(this@MainActivity).apply {
                text = "готов к работе"
                setTextColor(COLOR_TEXT_MUTED)
                setTextSize(TypedValue.COMPLEX_UNIT_SP, 11f)
                typeface = Typeface.MONOSPACE
            }
            statusChip.addView(tvStatus)
            addView(statusChip)
        }
    }

    private fun createHomeTab(): View {
        val scroll = ScrollView(this).apply {
            isFillViewport = true
            layoutParams = FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT)
        }

        val layout = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(16), dp(14), dp(16), dp(14))
        }

        // Дропзона Liquid Glass
        dropCard = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER
            setPadding(dp(20), dp(22), dp(20), dp(22))
            background = liquidGlassDrawable(COLOR_CARD_GLASS, COLOR_BORDER_GLOW, 18f)
            setOnClickListener { openFilePicker() }
        }

        val ivDropIcon = ImageView(this).apply {
            setImageDrawable(createGoogleIcon("folder", 40, COLOR_ACID))
            setPadding(0, 0, 0, dp(10))
        }
        dropCard.addView(ivDropIcon)

        val tvDropPrompt = TextView(this).apply {
            text = "Перетащите .jar или архив (.zip, .tar.gz, .7z, .rar) сюда"
            setTextColor(COLOR_TEXT)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 13.5f)
            typeface = Typeface.DEFAULT_BOLD
            gravity = Gravity.CENTER
        }
        dropCard.addView(tvDropPrompt)

        val tvDropSub = TextView(this).apply {
            text = "или нажмите, чтобы выбрать · .jar, .class, .zip, .rar, .7z"
            setTextColor(COLOR_TEXT_MUTED)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 11f)
            gravity = Gravity.CENTER
            setPadding(0, dp(4), 0, dp(14))
        }
        dropCard.addView(tvDropSub)

        val btnPick = Button(this).apply {
            text = "  Открыть .jar"
            setTextColor(Color.BLACK)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 13f)
            typeface = Typeface.DEFAULT_BOLD
            setCompoundDrawablesWithIntrinsicBounds(createGoogleIcon("folder", 18, Color.BLACK), null, null, null)
            background = liquidGlassDrawable(COLOR_ACID, COLOR_ACID, 16f)
            setPadding(dp(22), dp(8), dp(22), dp(8))
            setOnClickListener { openFilePicker() }
        }
        dropCard.addView(btnPick)
        layout.addView(dropCard)

        // Информационная карточка выбранного файла
        fileSelectedCard = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(16), dp(14), dp(16), dp(14))
            val lp = LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT)
            lp.topMargin = dp(14)
            layoutParams = lp
            background = liquidGlassDrawable(COLOR_SURFACE_GLASS, COLOR_BORDER, 16f)
        }

        tvFileTitle = TextView(this).apply {
            text = "// очередь пуста"
            setTextColor(COLOR_TEXT_MUTED)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 13f)
            typeface = Typeface.MONOSPACE
        }
        fileSelectedCard.addView(tvFileTitle)

        tvFileMeta = TextView(this).apply {
            text = "Добавьте архив (.jar, .zip, .tar.gz, .7z, .rar) или .class"
            setTextColor(COLOR_TEXT_MUTED)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 11f)
            setPadding(0, dp(4), 0, dp(12))
        }
        fileSelectedCard.addView(tvFileMeta)

        btnStartDecompile = Button(this).apply {
            text = "  Запустить декомпиляцию"
            setTextColor(Color.BLACK)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 13f)
            typeface = Typeface.DEFAULT_BOLD
            setCompoundDrawablesWithIntrinsicBounds(createGoogleIcon("play", 16, Color.BLACK), null, null, null)
            background = liquidGlassDrawable(COLOR_ACID, COLOR_ACID, 12f)
            isEnabled = false
            alpha = 0.4f
            setOnClickListener {
                if (isDecompiling) {
                    stopDecompilation()
                } else {
                    startDecompilation()
                }
            }
        }
        fileSelectedCard.addView(btnStartDecompile)

        pbDecompile = ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal).apply {
            isIndeterminate = false
            max = 100
            progress = 0
            visibility = View.GONE
            val lp = LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(4))
            lp.topMargin = dp(10)
            layoutParams = lp
        }
        fileSelectedCard.addView(pbDecompile)

        layout.addView(fileSelectedCard)
        scroll.addView(layout)

        animateDropzonePulse()
        return scroll
    }

    private fun animateDropzonePulse() {
        val anim = ObjectAnimator.ofFloat(dropCard, "alpha", 0.92f, 1.0f).apply {
            duration = 1800
            repeatCount = ValueAnimator.INFINITE
            repeatMode = ValueAnimator.REVERSE
            interpolator = AccelerateDecelerateInterpolator()
        }
        anim.start()
    }

    private fun createQueueTab(): View {
        val scroll = ScrollView(this).apply {
            isFillViewport = true
            layoutParams = FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT)
        }

        val layout = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(16), dp(16), dp(16), dp(16))
        }

        val headerLayout = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            setPadding(0, 0, 0, dp(12))
        }

        val tvQueueTitle = TextView(this).apply {
            text = "Входные архивы"
            setTextColor(COLOR_TEXT)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 15f)
            typeface = Typeface.DEFAULT_BOLD
            layoutParams = LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1.0f)
        }
        headerLayout.addView(tvQueueTitle)

        val btnClear = Button(this).apply {
            text = "Очистить"
            setTextColor(COLOR_TEXT_MUTED)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 11f)
            background = liquidGlassDrawable(COLOR_SURFACE_GLASS, COLOR_BORDER, 8f)
            setOnClickListener {
                queueFiles.clear()
                refreshQueueView()
            }
        }
        headerLayout.addView(btnClear)
        layout.addView(headerLayout)

        queueContainer = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
        }
        layout.addView(queueContainer)

        scroll.addView(layout)
        return scroll
    }

    // ПОЛНОЦЕННЫЙ РЕДАКТОР КОДА С ПОДСВЕТКОЙ СИНТАКСИСА, КОПИРОВАНИЕМ И ПОИСКОМ КЛАССОВ
    private fun createEditorTab(): View {
        val layout = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(12), dp(8), dp(12), dp(8))
            layoutParams = FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT)
        }

        // Панель инструментов редактора
        val editorToolbar = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            setPadding(0, 0, 0, dp(8))
        }

        tvCodePath = TextView(this).apply {
            text = "Файлы не открыты"
            setTextColor(COLOR_CYAN)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 11.5f)
            typeface = Typeface.MONOSPACE
            layoutParams = LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1.0f)
        }
        editorToolbar.addView(tvCodePath)

        btnToggleWrap = Button(this).apply {
            text = "Wrap"
            setTextColor(if (optWordWrap) COLOR_ACID else COLOR_TEXT_MUTED)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 10f)
            background = liquidGlassDrawable(COLOR_SURFACE_GLASS, COLOR_BORDER, 6f)
            setPadding(dp(8), dp(4), dp(8), dp(4))
            val lp = LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, dp(28))
            lp.marginEnd = dp(6)
            layoutParams = lp
            setOnClickListener {
                optWordWrap = !optWordWrap
                setTextColor(if (optWordWrap) COLOR_ACID else COLOR_TEXT_MUTED)
                reloadCodeViewer()
            }
        }
        editorToolbar.addView(btnToggleWrap)

        btnCopyCode = Button(this).apply {
            text = "Копировать"
            setTextColor(Color.BLACK)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 10.5f)
            typeface = Typeface.DEFAULT_BOLD
            setCompoundDrawablesWithIntrinsicBounds(createGoogleIcon("copy", 14, Color.BLACK), null, null, null)
            background = liquidGlassDrawable(COLOR_ACID, COLOR_ACID, 8f)
            setPadding(dp(10), dp(4), dp(10), dp(4))
            val lp = LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, dp(28))
            lp.marginEnd = dp(6)
            layoutParams = lp
            setOnClickListener { copyActiveCodeToClipboard() }
        }
        editorToolbar.addView(btnCopyCode)

        btnShareZip = Button(this).apply {
            text = "ZIP"
            setTextColor(Color.WHITE)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 10.5f)
            typeface = Typeface.DEFAULT_BOLD
            setCompoundDrawablesWithIntrinsicBounds(createGoogleIcon("share", 14, Color.WHITE), null, null, null)
            background = liquidGlassDrawable(Color.parseColor("#1C2A38"), Color.parseColor("#344A60"), 8f)
            setPadding(dp(10), dp(4), dp(10), dp(4))
            layoutParams = LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, dp(28))
            setOnClickListener { shareActiveProjectZip() }
        }
        editorToolbar.addView(btnShareZip)
        layout.addView(editorToolbar)

        // Поле быстрого поиска по классам
        etClassFilter = EditText(this).apply {
            hint = "Поиск класса в плагине..."
            setHintTextColor(COLOR_TEXT_MUTED)
            setTextColor(COLOR_TEXT)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 11f)
            typeface = Typeface.MONOSPACE
            background = liquidGlassDrawable(COLOR_SURFACE_GLASS, COLOR_BORDER, 8f)
            setPadding(dp(10), dp(6), dp(10), dp(6))
            val lp = LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT)
            lp.bottomMargin = dp(6)
            layoutParams = lp
            addTextChangedListener(object : TextWatcher {
                override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {}
                override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {
                    filterClassTree(s?.toString() ?: "")
                }
                override fun afterTextChanged(s: Editable?) {}
            })
        }
        layout.addView(etClassFilter)

        // Сплит-панель: Список классов (вверху) + Текст кода с номерами строк (внизу)
        val splitLayout = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1.0f)
        }

        // Горизонтальный / вертикальный список файлов плагина
        val scrollFiles = ScrollView(this).apply {
            layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, dp(110))
            background = liquidGlassDrawable(COLOR_SURFACE_GLASS, COLOR_BORDER, 10f)
            setPadding(dp(6), dp(4), dp(6), dp(4))
        }
        sourceFilesContainer = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
        }
        scrollFiles.addView(sourceFilesContainer)
        splitLayout.addView(scrollFiles)

        // Область кода с номерами строк и поддержкой горизонтального скролла
        val codeContainerCard = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            val lp = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1.0f)
            lp.topMargin = dp(6)
            layoutParams = lp
            background = liquidGlassDrawable(Color.parseColor("#04070A"), COLOR_BORDER, 10f)
        }

        val scrollCodeVertical = ScrollView(this).apply {
            layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1.0f)
        }

        val scrollCodeHorizontal = HorizontalScrollView(this).apply {
            layoutParams = FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.WRAP_CONTENT)
        }

        val codeWithLinesLayout = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            setPadding(dp(8), dp(8), dp(8), dp(8))
        }

        // Колонка номеров строк
        tvLineNumbers = TextView(this).apply {
            text = "1"
            setTextColor(Color.parseColor("#445262"))
            setTextSize(TypedValue.COMPLEX_UNIT_SP, optEditorFontSize)
            typeface = Typeface.MONOSPACE
            gravity = Gravity.END
            setPadding(0, 0, dp(10), 0)
        }
        codeWithLinesLayout.addView(tvLineNumbers)

        // Сам текст кода с синтаксической раскраской
        tvCodeViewer = TextView(this).apply {
            text = "// Выберите класс или файл из списка выше для просмотра"
            setTextColor(COLOR_TEXT)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, optEditorFontSize)
            typeface = Typeface.MONOSPACE
            setTextIsSelectable(true)
        }
        codeWithLinesLayout.addView(tvCodeViewer)

        scrollCodeHorizontal.addView(codeWithLinesLayout)
        scrollCodeVertical.addView(scrollCodeHorizontal)
        codeContainerCard.addView(scrollCodeVertical)

        // Футер редактора в стиле ПК: L · c · RO
        tvCodeFooterMeta = TextView(this).apply {
            text = "0 строк · 0 байт · Read-Only Java AST"
            setTextColor(COLOR_TEXT_MUTED)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 10f)
            typeface = Typeface.MONOSPACE
            setPadding(dp(8), dp(3), dp(8), dp(3))
            setBackgroundColor(Color.parseColor("#080D13"))
        }
        codeContainerCard.addView(tvCodeFooterMeta)

        splitLayout.addView(codeContainerCard)
        layout.addView(splitLayout)
        return layout
    }

    private fun createTerminalTab(): View {
        val layout = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(10), dp(10), dp(10), dp(10))
            layoutParams = FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT)
        }

        svTerminal = ScrollView(this).apply {
            layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1.0f)
            background = liquidGlassDrawable(Color.parseColor("#030608"), COLOR_BORDER_GLOW, 12f)
            setPadding(dp(10), dp(10), dp(10), dp(10))
        }

        tvTerminal = TextView(this).apply {
            text = ""
            setTextColor(COLOR_ACID)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 10.5f)
            typeface = Typeface.MONOSPACE
            setTextIsSelectable(true)
        }
        svTerminal.addView(tvTerminal)
        layout.addView(svTerminal)

        return layout
    }

    // ПОЛНОЦЕННАЯ ВКЛАДКА НАСТРОЕК (ПК 1:1)
    private fun createSettingsTab(): View {
        val scroll = ScrollView(this).apply {
            isFillViewport = true
            layoutParams = FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT)
        }

        val layout = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(16), dp(14), dp(16), dp(16))
        }

        fun addSection(title: String) {
            val tv = TextView(this).apply {
                text = title.uppercase()
                setTextColor(COLOR_ACID)
                setTextSize(TypedValue.COMPLEX_UNIT_SP, 11f)
                typeface = Typeface.DEFAULT_BOLD
                setPadding(0, dp(14), 0, dp(6))
            }
            layout.addView(tv)
        }

        fun addSwitchCard(title: String, desc: String, initialVal: Boolean, onToggle: (Boolean) -> Unit) {
            val card = LinearLayout(this).apply {
                orientation = LinearLayout.HORIZONTAL
                gravity = Gravity.CENTER_VERTICAL
                setPadding(dp(14), dp(10), dp(14), dp(10))
                val lp = LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT)
                lp.bottomMargin = dp(6)
                layoutParams = lp
                background = liquidGlassDrawable(COLOR_SURFACE_GLASS, COLOR_BORDER, 12f)
            }

            val textLayout = LinearLayout(this).apply {
                orientation = LinearLayout.VERTICAL
                layoutParams = LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1.0f)
            }
            val t = TextView(this).apply {
                text = title
                setTextColor(COLOR_TEXT)
                setTextSize(TypedValue.COMPLEX_UNIT_SP, 13f)
                typeface = Typeface.DEFAULT_BOLD
            }
            val d = TextView(this).apply {
                text = desc
                setTextColor(COLOR_TEXT_MUTED)
                setTextSize(TypedValue.COMPLEX_UNIT_SP, 10.5f)
            }
            textLayout.addView(t)
            textLayout.addView(d)
            card.addView(textLayout)

            val sw = Switch(this).apply {
                isChecked = initialVal
                setOnCheckedChangeListener { _, isChecked ->
                    onToggle(isChecked)
                    vibrate(30)
                }
            }
            card.addView(sw)
            layout.addView(card)
        }

        // 1. Декомпилятор (Ядро C++20 / Rust FastScan)
        addSection("Ядро декомпилятора C++20")
        addSwitchCard("Деобфускация строк", "Инлайн AegisVM, Allatori, Dynamic XOR", optDeobfuscateStrings) { optDeobfuscateStrings = it }
        addSwitchCard("Восстановление AST", "Циклы while / for, блоки switch-case", optRestoreControlFlow) { optRestoreControlFlow = it }
        addSwitchCard("Проверка легитимности", "Сверка хеша Modrinth, Spiget, GitHub", optLegitimacyCheck) { optLegitimacyCheck = it }
        addSwitchCard("Поиск уязвимостей", "Детекция ForceOP, Discord-вебхуков, RCE", optVulnerabilityScan) { optVulnerabilityScan = it }

        // 2. Редактор кода
        addSection("Редактор кода")
        addSwitchCard("Подсветка синтаксиса", "Раскраска Java ключевых слов и аннотаций", optSyntaxHighlight) {
            optSyntaxHighlight = it
            reloadCodeViewer()
        }
        addSwitchCard("Виброотклик", "Тактильная отдача при нажатиях", optVibration) { optVibration = it }

        // 3. О программе
        addSection("О программе")
        val aboutCard = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(14), dp(12), dp(14), dp(12))
            background = liquidGlassDrawable(COLOR_SURFACE_GLASS, COLOR_BORDER, 12f)
        }
        val tvAbout = TextView(this).apply {
            text = "NanoDecompiler Mobile v2.0.3 ARM64\n" +
                    "Автор: NanoDev\n" +
                    "Канал: t.me/WineSquad\n" +
                    "Связь: t.me/radoqi\n" +
                    "GitHub: nanodev1488\n\n" +
                    "Лицензия: Полная версия без ограничений"
            setTextColor(COLOR_TEXT)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 11f)
            typeface = Typeface.MONOSPACE
        }
        aboutCard.addView(tvAbout)
        layout.addView(aboutCard)

        scroll.addView(layout)
        return scroll
    }

    private fun createBottomNav(): LinearLayout {
        val nav = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            setBackgroundColor(COLOR_SURFACE)
            setPadding(dp(4), dp(6), dp(4), dp(6))
        }

        navButtons.clear()
        val tabIcons = arrayOf("folder", "queue", "code", "terminal", "settings")
        val tabTitles = arrayOf("Главная", "Архивы", "Редактор", "Терминал", "Настройки")
        for (i in tabTitles.indices) {
            val btn = Button(this).apply {
                text = tabTitles[i]
                setTextColor(if (i == 0) COLOR_ACID else COLOR_TEXT_MUTED)
                setTextSize(TypedValue.COMPLEX_UNIT_SP, 10f)
                setCompoundDrawablesWithIntrinsicBounds(null, createGoogleIcon(tabIcons[i], 18, if (i == 0) COLOR_ACID else COLOR_TEXT_MUTED), null, null)
                background = if (i == 0) liquidGlassDrawable(COLOR_SURFACE_GLASS, COLOR_BORDER, 10f) else null
                layoutParams = LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1.0f)
                setOnClickListener { switchTab(i) }
            }
            navButtons.add(btn)
            nav.addView(btn)
        }
        return nav
    }

    private fun switchTab(index: Int) {
        currentTab = index
        tabHomeView.visibility = if (index == 0) View.VISIBLE else View.GONE
        tabQueueView.visibility = if (index == 1) View.VISIBLE else View.GONE
        tabEditorView.visibility = if (index == 2) View.VISIBLE else View.GONE
        tabTerminalView.visibility = if (index == 3) View.VISIBLE else View.GONE
        tabSettingsView.visibility = if (index == 4) View.VISIBLE else View.GONE

        val tabIcons = arrayOf("folder", "queue", "code", "terminal", "settings")
        for (i in navButtons.indices) {
            val btn = navButtons[i]
            val active = (i == index)
            btn.setTextColor(if (active) COLOR_ACID else COLOR_TEXT_MUTED)
            btn.setCompoundDrawablesWithIntrinsicBounds(null, createGoogleIcon(tabIcons[i], 18, if (active) COLOR_ACID else COLOR_TEXT_MUTED), null, null)
            btn.background = if (active) liquidGlassDrawable(COLOR_SURFACE_GLASS, COLOR_BORDER, 10f) else null
        }
    }

    private fun openFilePicker() {
        val intent = Intent(Intent.ACTION_GET_CONTENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE)
            type = "*/*"
            putExtra(Intent.EXTRA_MIME_TYPES, arrayOf(
                "application/java-archive",
                "application/zip",
                "application/octet-stream",
                "*/*"
            ))
        }
        startActivityForResult(Intent.createChooser(intent, "Выберите .jar или .class"), 2001)
    }

    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode == 2001 && resultCode == RESULT_OK && data?.data != null) {
            val uri = data.data!!
            handleIncomingUri(uri)
        }
    }

    private fun handleIncomingUri(uri: Uri) {
        bgExecutor.execute {
            try {
                var fileName = "plugin.jar"
                contentResolver.query(uri, null, null, null, null)?.use { cursor ->
                    val nameIdx = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME)
                    if (nameIdx != -1 && cursor.moveToFirst()) {
                        fileName = cursor.getString(nameIdx)
                    }
                }

                if (!FastScanEngine.isSupportedFile(fileName)) {
                    runOnUiThread {
                        vibrate(100)
                        Toast.makeText(this, "Ошибка: выберите .jar, .class или zip архив!", Toast.LENGTH_LONG).show()
                    }
                    return@execute
                }

                val cacheFile = File(cacheDir, fileName)
                contentResolver.openInputStream(uri)?.use { inStream ->
                    FileOutputStream(cacheFile).use { outStream ->
                        inStream.copyTo(outStream)
                    }
                }

                activeFile = cacheFile
                if (!queueFiles.contains(cacheFile)) queueFiles.add(cacheFile)

                val rep = FastScanEngine.scan(cacheFile)
                activeReport = rep

                runOnUiThread {
                    tvFileTitle.text = rep.fileName
                    val sizeKb = rep.sizeBytes / 1024
                    tvFileMeta.text = "Размер: $sizeKb КБ • Классов: ${rep.totalClasses} • Энтропия: ${rep.maxEntropy}\n" +
                            "Плагин: ${rep.pluginName} v${rep.version} • Автор: ${rep.author}\n" +
                            "Защита: ${if (rep.isNanoGuardProtected) "NanoGuard DETECTED" else "Чисто"} • Угрозы: ${rep.threatLevel}"
                    btnStartDecompile.isEnabled = true
                    btnStartDecompile.alpha = 1.0f
                    tvStatus.text = "в очереди"
                    statusDot.background = liquidGlassDrawable(COLOR_CYAN, COLOR_CYAN, 4f)
                    refreshQueueView()
                    Toast.makeText(this, "Готов к декомпиляции: ${rep.fileName}", Toast.LENGTH_SHORT).show()
                }

            } catch (e: Exception) {
                runOnUiThread {
                    Toast.makeText(this, "Ошибка обработки: ${e.message}", Toast.LENGTH_LONG).show()
                }
            }
        }
    }

    private fun refreshQueueView() {
        queueContainer.removeAllViews()
        for (f in queueFiles) {
            val card = LinearLayout(this).apply {
                orientation = LinearLayout.HORIZONTAL
                gravity = Gravity.CENTER_VERTICAL
                setPadding(dp(14), dp(12), dp(14), dp(12))
                val lp = LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT)
                lp.bottomMargin = dp(8)
                layoutParams = lp
                background = liquidGlassDrawable(COLOR_SURFACE_GLASS, COLOR_BORDER, 12f)
            }

            val dot = View(this).apply {
                background = liquidGlassDrawable(COLOR_ACID, COLOR_ACID, 4f)
                val lp = LinearLayout.LayoutParams(dp(8), dp(8))
                lp.marginEnd = dp(10)
                layoutParams = lp
            }
            card.addView(dot)

            val tvName = TextView(this).apply {
                text = "${f.name} (${f.length() / 1024} КБ)"
                setTextColor(COLOR_TEXT)
                setTextSize(TypedValue.COMPLEX_UNIT_SP, 13f)
                typeface = Typeface.MONOSPACE
                layoutParams = LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1.0f)
            }
            card.addView(tvName)

            val btnDel = ImageButton(this).apply {
                setImageDrawable(createGoogleIcon("delete", 16, COLOR_DANGER))
                background = null
                setOnClickListener {
                    queueFiles.remove(f)
                    refreshQueueView()
                }
            }
            card.addView(btnDel)
            queueContainer.addView(card)
        }
    }

    private fun startDecompilation() {
        val target = activeFile ?: return
        isDecompiling = true
        btnStartDecompile.text = "  Остановить"
        btnStartDecompile.setTextColor(Color.WHITE)
        btnStartDecompile.setCompoundDrawablesWithIntrinsicBounds(createGoogleIcon("stop", 16, Color.WHITE), null, null, null)
        btnStartDecompile.background = liquidGlassDrawable(COLOR_DANGER, COLOR_DANGER, 12f)
        btnStartDecompile.isEnabled = true
        btnStartDecompile.alpha = 1.0f

        pbDecompile.visibility = View.VISIBLE
        pbDecompile.progress = 10
        tvStatus.text = "декомпиляция..."
        statusDot.background = liquidGlassDrawable(COLOR_ACID, COLOR_ACID, 4f)

        bgExecutor.execute {
            val res = DecompilerEngine.decompile(
                this,
                target,
                onStage = { stage ->
                    runOnUiThread {
                        when (stage.lowercase()) {
                            "pass1" -> {
                                tvStatus.text = "[1/3] Чтение классов"
                                pbDecompile.progress = 33
                            }
                            "pass2" -> {
                                tvStatus.text = "[2/3] Анализ AST"
                                pbDecompile.progress = 66
                            }
                            "pass3" -> {
                                tvStatus.text = "[3/3] Генерация Java"
                                pbDecompile.progress = 90
                            }
                            "done" -> {
                                tvStatus.text = "Готово"
                                pbDecompile.progress = 100
                            }
                            else -> {
                                tvStatus.text = stage
                            }
                        }
                    }
                },
                onLog = { line ->
                    runOnUiThread { logTerminal(line) }
                }
            )

            runOnUiThread {
                pbDecompile.visibility = View.GONE
                resetDecompileButton()

                if (res.ok) {
                    tvStatus.text = "готов к работе"
                    statusDot.background = liquidGlassDrawable(COLOR_ACID, COLOR_ACID, 4f)
                    activeOutDir = File(res.outDir)
                    populateSourceTree(activeOutDir!!)
                    vibrate(50)
                    Toast.makeText(this, "Готово: ${target.name} — ${String.format("%.2f", res.elapsedSeconds)} s", Toast.LENGTH_LONG).show()
                    switchTab(2) // Переход к вкладке Редактора
                } else {
                    tvStatus.text = "ошибка"
                    statusDot.background = liquidGlassDrawable(COLOR_DANGER, COLOR_DANGER, 4f)
                    vibrate(150)
                    Toast.makeText(this, "Ошибка декомпиляции: ${res.error}", Toast.LENGTH_LONG).show()
                }
            }
        }
    }

    private fun populateSourceTree(outDir: File) {
        decompiledFilesList.clear()
        outDir.walkTopDown().filter { it.isFile && (it.extension == "java" || it.extension == "yml" || it.extension == "json") }.forEach {
            decompiledFilesList.add(it)
        }
        filterClassTree("")
        if (decompiledFilesList.isNotEmpty()) {
            openFileInEditor(decompiledFilesList.first(), outDir)
        }
    }

    private fun filterClassTree(query: String) {
        sourceFilesContainer.removeAllViews()
        val outDir = activeOutDir ?: return
        val filtered = if (query.isEmpty()) decompiledFilesList else decompiledFilesList.filter {
            it.name.contains(query, ignoreCase = true) || it.relativeTo(outDir).path.contains(query, ignoreCase = true)
        }

        for (file in filtered) {
            val relPath = file.relativeTo(outDir).path.replace('\\', '/')
            val itemLayout = LinearLayout(this).apply {
                orientation = LinearLayout.HORIZONTAL
                gravity = Gravity.CENTER_VERTICAL
                setPadding(dp(6), dp(4), dp(6), dp(4))
                background = if (file == currentActiveFile) liquidGlassDrawable(COLOR_CARD_GLASS, COLOR_ACID, 6f) else null
                setOnClickListener {
                    openFileInEditor(file, outDir)
                }
            }

            val iconCode = ImageView(this).apply {
                setImageDrawable(createGoogleIcon("code", 14, if (relPath.endsWith(".java")) COLOR_ACID else COLOR_CYAN))
                val lp = LinearLayout.LayoutParams(dp(16), dp(16))
                lp.marginEnd = dp(6)
                layoutParams = lp
            }
            itemLayout.addView(iconCode)

            val tvItem = TextView(this).apply {
                text = relPath
                setTextColor(if (file == currentActiveFile) COLOR_ACID else COLOR_TEXT)
                setTextSize(TypedValue.COMPLEX_UNIT_SP, 11f)
                typeface = Typeface.MONOSPACE
            }
            itemLayout.addView(tvItem)
            sourceFilesContainer.addView(itemLayout)
        }
    }

    private fun openFileInEditor(file: File, outDir: File) {
        currentActiveFile = file
        val relPath = file.relativeTo(outDir).path.replace('\\', '/')
        tvCodePath.text = relPath

        bgExecutor.execute {
            val rawText = try {
                file.readText(StandardCharsets.UTF_8)
            } catch (e: Exception) {
                "// Ошибка чтения файла: ${e.message}"
            }

            val lineCount = rawText.lines().size
            val sizeBytes = file.length()
            val highlighted = if (optSyntaxHighlight && file.extension == "java") highlightJavaCode(rawText) else SpannableStringBuilder(rawText)

            val lineNumsBuilder = StringBuilder()
            for (i in 1..lineCount) {
                lineNumsBuilder.append(i).append("\n")
            }
            val lineNumsStr = lineNumsBuilder.toString()

            runOnUiThread {
                tvLineNumbers.text = lineNumsStr
                tvCodeViewer.text = highlighted
                tvCodeFooterMeta.text = "$lineCount строк · $sizeBytes Б · Read-Only Java AST"
                filterClassTree(etClassFilter.text.toString())
            }
        }
    }

    private fun reloadCodeViewer() {
        val f = currentActiveFile
        val d = activeOutDir
        if (f != null && d != null) {
            openFileInEditor(f, d)
        }
    }

    private fun copyActiveCodeToClipboard() {
        val text = tvCodeViewer.text.toString()
        if (text.isEmpty() || text.startsWith("// Выберите класс")) {
            Toast.makeText(this, "Нечего копировать", Toast.LENGTH_SHORT).show()
            return
        }
        val clipboard = getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
        val clip = ClipData.newPlainText("Decompiled Java Source", text)
        clipboard.setPrimaryClip(clip)
        vibrate(40)
        Toast.makeText(this, "Исходный код скопирован в буфер обмена!", Toast.LENGTH_SHORT).show()
    }

    private fun shareActiveProjectZip() {
        val outDir = activeOutDir
        if (outDir == null || !outDir.exists() || outDir.listFiles().isNullOrEmpty()) {
            Toast.makeText(this, "Нет декомпилированных файлов для отправки", Toast.LENGTH_SHORT).show()
            return
        }

        Toast.makeText(this, "Упаковка ZIP-архива с исходниками...", Toast.LENGTH_SHORT).show()
        vibrate(40)

        bgExecutor.execute {
            try {
                // Создаем ZIP рядом с распакованной папкой (в Загрузках или в external files)
                val baseParent = outDir.parentFile ?: getExternalFilesDir(android.os.Environment.DIRECTORY_DOWNLOADS) ?: cacheDir
                val zipFile = File(baseParent, "${outDir.name}-src.zip")
                if (zipFile.exists()) zipFile.delete()

                java.util.zip.ZipOutputStream(java.io.FileOutputStream(zipFile)).use { zos ->
                    val rootPath = outDir.absolutePath
                    outDir.walkTopDown().filter { it.isFile }.forEach { file ->
                        val relName = file.absolutePath.removePrefix(rootPath).trimStart(File.separatorChar, '/', '\\').replace('\\', '/')
                        val entry = java.util.zip.ZipEntry(relName)
                        zos.putNextEntry(entry)
                        file.inputStream().use { input -> input.copyTo(zos) }
                        zos.closeEntry()
                    }
                }

                val uri = NanoFileProvider.getUriForFile(this, zipFile)
                runOnUiThread {
                    vibrate(60)
                    val shareIntent = Intent(Intent.ACTION_SEND).apply {
                        type = "application/zip"
                        putExtra(Intent.EXTRA_STREAM, uri)
                        putExtra(Intent.EXTRA_SUBJECT, "Исходники ${outDir.name}")
                        putExtra(Intent.EXTRA_TEXT, "Декомпилированные исходники ${outDir.name} (NanoDecompiler Mobile)")
                        addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
                    }
                    startActivity(Intent.createChooser(shareIntent, "Поделиться архивом исходников (.zip)"))
                }
            } catch (e: Exception) {
                runOnUiThread {
                    Toast.makeText(this, "Ошибка создания ZIP: ${e.message}", Toast.LENGTH_LONG).show()
                }
            }
        }
    }

    // Быстрый движок подсветки синтаксиса Java
    private fun highlightJavaCode(code: String): SpannableStringBuilder {
        val ssb = SpannableStringBuilder(code)

        val KEYWORDS = setOf(
            "package", "import", "public", "private", "protected", "class", "interface", "enum",
            "extends", "implements", "void", "return", "if", "else", "for", "while", "do", "switch",
            "case", "default", "break", "continue", "try", "catch", "finally", "throw", "throws",
            "new", "null", "true", "false", "static", "final", "abstract", "synchronized", "volatile",
            "transient", "native", "super", "this", "instanceof", "int", "boolean", "byte", "char",
            "short", "long", "float", "double"
        )

        // Ключевые слова
        val wordPattern = Pattern.compile("\\b([A-Za-z_][A-Za-z0-9_]*)\\b")
        val matcher = wordPattern.matcher(code)
        while (matcher.find()) {
            val word = matcher.group(1)
            if (KEYWORDS.contains(word)) {
                ssb.setSpan(ForegroundColorSpan(COLOR_SYNTAX_KEYWORD), matcher.start(), matcher.end(), Spannable.SPAN_EXCLUSIVE_EXCLUSIVE)
                ssb.setSpan(StyleSpan(Typeface.BOLD), matcher.start(), matcher.end(), Spannable.SPAN_EXCLUSIVE_EXCLUSIVE)
            } else if (word != null && word[0].isUpperCase()) {
                ssb.setSpan(ForegroundColorSpan(COLOR_SYNTAX_TYPE), matcher.start(), matcher.end(), Spannable.SPAN_EXCLUSIVE_EXCLUSIVE)
            }
        }

        // Аннотации (@Override, @EventHandler)
        val annotPattern = Pattern.compile("@[A-Za-z0-9_]+")
        val annotMatcher = annotPattern.matcher(code)
        while (annotMatcher.find()) {
            ssb.setSpan(ForegroundColorSpan(COLOR_SYNTAX_ANNOTATION), annotMatcher.start(), annotMatcher.end(), Spannable.SPAN_EXCLUSIVE_EXCLUSIVE)
        }

        // Строковые литералы ("...")
        val strPattern = Pattern.compile("\"(\\\\.|[^\"])*\"")
        val strMatcher = strPattern.matcher(code)
        while (strMatcher.find()) {
            ssb.setSpan(ForegroundColorSpan(COLOR_SYNTAX_STRING), strMatcher.start(), strMatcher.end(), Spannable.SPAN_EXCLUSIVE_EXCLUSIVE)
        }

        // Числа
        val numPattern = Pattern.compile("\\b\\d+([._]\\d+)?[fFlL]?\\b")
        val numMatcher = numPattern.matcher(code)
        while (numMatcher.find()) {
            ssb.setSpan(ForegroundColorSpan(COLOR_SYNTAX_NUMBER), numMatcher.start(), numMatcher.end(), Spannable.SPAN_EXCLUSIVE_EXCLUSIVE)
        }

        // Комментарии (// ...)
        val commentPattern = Pattern.compile("//[^\n]*")
        val commentMatcher = commentPattern.matcher(code)
        while (commentMatcher.find()) {
            ssb.setSpan(ForegroundColorSpan(COLOR_SYNTAX_COMMENT), commentMatcher.start(), commentMatcher.end(), Spannable.SPAN_EXCLUSIVE_EXCLUSIVE)
        }

        return ssb
    }

    private fun logTerminal(line: String) {
        tvTerminal.append(line + "\n")
        svTerminal.post { svTerminal.fullScroll(View.FOCUS_DOWN) }
    }

    private fun vibrate(ms: Long) {
        if (!optVibration) return
        try {
            vibrator?.let { if (it.hasVibrator()) it.vibrate(ms) }
        } catch (_: Exception) {}
    }
}
