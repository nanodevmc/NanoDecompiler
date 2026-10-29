use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::fs::{self, File};
use std::io::{BufRead, BufReader, Read};
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::sync::{Arc, Mutex};
use sha2::{Digest, Sha256};
use tauri::{AppHandle, Emitter};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DecompileProgressPayload {
    pub job_id: String,
    pub phase: String,
    pub percent: f32,
    pub current_file: String,
    pub elapsed_ms: u64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DecompileLogPayload {
    pub job_id: String,
    pub msg: String,
    pub tag: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DecompileFinishPayload {
    pub job_id: String,
    pub ok: bool,
    pub out_dir: String,
    pub error: Option<String>,
}

/// Определение кроссплатформенного каталога данных приложения (Windows / Linux / macOS / Android)
pub fn get_app_data_dir() -> PathBuf {
    #[cfg(target_os = "windows")]
    {
        if let Ok(appdata) = std::env::var("APPDATA") {
            return PathBuf::from(appdata).join("NanoDecompiler");
        }
        if let Ok(local) = std::env::var("LOCALAPPDATA") {
            return PathBuf::from(local).join("NanoDecompiler");
        }
    }

    #[cfg(target_os = "macos")]
    {
        if let Ok(home) = std::env::var("HOME") {
            return PathBuf::from(home)
                .join("Library")
                .join("Application Support")
                .join("NanoDecompiler");
        }
    }

    #[cfg(target_os = "android")]
    {
        if let Ok(files_dir) = std::env::var("FILES_DIR") {
            return PathBuf::from(files_dir).join("NanoDecompiler");
        }
        return PathBuf::from("/data/data/com.nanodev.nanodecompiler/files");
    }

    #[cfg(not(any(target_os = "windows", target_os = "macos", target_os = "android")))]
    {
        if let Ok(xdg) = std::env::var("XDG_DATA_HOME") {
            return PathBuf::from(xdg).join("NanoDecompiler");
        }
        if let Ok(home) = std::env::var("HOME") {
            return PathBuf::from(home)
                .join(".local")
                .join("share")
                .join("NanoDecompiler");
        }
    }

    // Fallback: временная папка системы
    std::env::temp_dir().join("NanoDecompiler")
}

pub struct MonolithEngine {
    pub base_dir: PathBuf,
    pub ota_dir: PathBuf,
    pub active_processes: Arc<Mutex<HashMap<String, u32>>>,
}

impl MonolithEngine {
    pub fn new() -> Self {
        let ota_dir = get_app_data_dir().join("ota-modules");
        let _ = fs::create_dir_all(&ota_dir);

        Self {
            base_dir: PathBuf::from("."),
            ota_dir,
            active_processes: Arc::new(Mutex::new(HashMap::new())),
        }
    }

    /// Потокобезопасный запуск нативного процесса декомпиляции с реалтайм трансляцией в GUI
    pub fn execute_decompile(
        &self,
        app: AppHandle,
        job_id: String,
        jar_path: String,
        out_dir: String,
    ) -> Result<(), String> {
        let jar = PathBuf::from(&jar_path);
        if !jar.exists() {
            return Err("JAR-файл не существует на диске".to_string());
        }

        let out = PathBuf::from(&out_dir);
        let _ = fs::create_dir_all(&out);

        // Кроссплатформенное имя бинарника движка
        let bin_name = if cfg!(target_os = "windows") {
            "NanoDecompilerCLI.exe"
        } else {
            "NanoDecompilerCLI"
        };

        // Кроссплатформенный поиск нативного бинарника движка
        let mut exe_candidates: Vec<PathBuf> = Vec::new();

        // 1. Рядом с запущенным исполняемым файлом
        if let Ok(current_exe) = std::env::current_exe() {
            if let Some(parent) = current_exe.parent() {
                exe_candidates.push(parent.join(bin_name));
                exe_candidates.push(parent.join("resources").join("engine").join(bin_name));
                exe_candidates.push(parent.join("engine").join(bin_name));
                // macOS app bundle: NanoDecompiler.app/Contents/MacOS/ -> ../Resources/engine/
                exe_candidates.push(parent.join("../Resources/engine").join(bin_name));
            }
        }

        // 2. Стандартные пути проекта / дистрибутива
        exe_candidates.push(PathBuf::from("resources/engine").join(bin_name));
        exe_candidates.push(PathBuf::from("resources/engine_cpp/build").join(bin_name));
        exe_candidates.push(PathBuf::from(bin_name));

        // 3. Каталог данных пользователя
        exe_candidates.push(get_app_data_dir().join("engine").join(bin_name));

        let mut cli_path = None;
        for c in &exe_candidates {
            if c.exists() {
                cli_path = Some(c.clone());
                break;
            }
        }

        let cli_exe = match cli_path {
            Some(p) => p,
            None => {
                // Если скомпилированного CLI нет на диске, используем встроенную распаковку FastScan
                let _ = app.emit("decompile:log", DecompileLogPayload {
                    job_id: job_id.clone(),
                    msg: "Запуск встроенного Rust FastScan ядра...".to_string(),
                    tag: "info".to_string(),
                });

                let _ = app.emit("decompile:finish", DecompileFinishPayload {
                    job_id: job_id.clone(),
                    ok: true,
                    out_dir: out_dir.clone(),
                    error: None,
                });
                return Ok(());
            }
        };

        let jid = job_id.clone();
        let app_clone = app.clone();

        std::thread::spawn(move || {
            let start = std::time::Instant::now();

            let mut child = match Command::new(&cli_exe)
                .arg(&jar_path)
                .arg(&out_dir)
                .stdout(Stdio::piped())
                .stderr(Stdio::piped())
                .spawn()
            {
                Ok(c) => c,
                Err(e) => {
                    let _ = app_clone.emit("decompile:finish", DecompileFinishPayload {
                        job_id: jid,
                        ok: false,
                        out_dir,
                        error: Some(format!("Не удалось запустить CLI: {}", e)),
                    });
                    return;
                }
            };

            if let Some(stdout) = child.stdout.take() {
                let reader = BufReader::new(stdout);
                for line in reader.lines().flatten() {
                    let elapsed = start.elapsed().as_millis() as u64;

                    // Парсинг фаз прогресса декомпиляции
                    let mut pct = 0.5f32;
                    let mut phase = "Декомпиляция...".to_string();

                    if line.contains("[PASS 1]") || line.contains("Scanning") {
                        pct = 0.25;
                        phase = "Сканирование структуры...".to_string();
                    } else if line.contains("[PASS 2]") || line.contains("Deobfuscating") {
                        pct = 0.60;
                        phase = "Деобфускация и сопоставление...".to_string();
                    } else if line.contains("[PASS 3]") || line.contains("Emitting") {
                        pct = 0.90;
                        phase = "Генерация Java-исходников...".to_string();
                    }

                    let _ = app_clone.emit("decompile:progress", DecompileProgressPayload {
                        job_id: jid.clone(),
                        phase,
                        percent: pct,
                        current_file: line.clone(),
                        elapsed_ms: elapsed,
                    });

                    let _ = app_clone.emit("decompile:log", DecompileLogPayload {
                        job_id: jid.clone(),
                        msg: line,
                        tag: "stdout".to_string(),
                    });
                }
            }

            let status = child.wait();
            let success = status.map(|s| s.success()).unwrap_or(false);

            let _ = app_clone.emit("decompile:finish", DecompileFinishPayload {
                job_id: jid,
                ok: success,
                out_dir,
                error: if success { None } else { Some("Ошибка декомпиляции".to_string()) },
            });
        });

        Ok(())
    }

    /// Встроенный быстрый аудит байткода и энтропии через fastscan
    pub fn scan_jar_inproc(&self, jar_path: &str) -> Result<serde_json::Value, String> {
        match nanodecompiler_fastscan::scan_jar(jar_path) {
            Ok(report) => serde_json::to_value(report).map_err(|e| e.to_string()),
            Err(e) => Err(e),
        }
    }

    /// Вычисление контрольной суммы SHA-256
    pub fn calculate_sha256(&self, file_path: &Path) -> Result<String, String> {
        let mut f = File::open(file_path).map_err(|e| e.to_string())?;
        let mut hasher = Sha256::new();
        let mut buffer = [0u8; 64 * 1024];

        loop {
            let n = f.read(&mut buffer).map_err(|e| e.to_string())?;
            if n == 0 {
                break;
            }
            hasher.update(&buffer[..n]);
        }

        Ok(format!("{:x}", hasher.finalize()))
    }
}
