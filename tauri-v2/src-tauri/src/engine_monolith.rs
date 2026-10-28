use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::fs::{self, File};
use std::io::{Read, Write};
use std::path::{Path, PathBuf};
use sha2::{Digest, Sha256};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct OtaModuleInfo {
    pub name: String,
    pub version: String,
    pub sha256: String,
    pub size_bytes: u64,
    pub url: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct OtaManifest {
    pub manifest_version: String,
    pub release_tag: String,
    pub generated_at: String,
    pub gui_modules: HashMap<String, OtaModuleInfo>,
    pub engine_modules: HashMap<String, OtaModuleInfo>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DecompileProgress {
    pub phase: String,
    pub percent: f32,
    pub current_file: String,
}

pub struct MonolithEngine {
    pub base_dir: PathBuf,
    pub ota_dir: PathBuf,
}

impl MonolithEngine {
    pub fn new() -> Self {
        let app_data = std::env::var("APPDATA").unwrap_or_else(|_| ".".to_string());
        let ota_dir = PathBuf::from(app_data).join("NanoDecompiler").join("ota-modules");
        let _ = fs::create_dir_all(&ota_dir);

        Self {
            base_dir: PathBuf::from("."),
            ota_dir,
        }
    }

    /// Быстрый аудит и сканирование JAR через встроенный FastScan крейт
    pub fn scan_jar_inproc(&self, jar_path: &str) -> Result<serde_json::Value, String> {
        match nanodecompiler_fastscan::scan_jar(jar_path) {
            Ok(report) => serde_json::to_value(report).map_err(|e| e.to_string()),
            Err(e) => Err(e),
        }
    }

    /// Вычисление SHA256 файла для верификации целостности OTA-модулей
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
