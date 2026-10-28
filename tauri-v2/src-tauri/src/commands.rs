use serde::{Deserialize, Serialize};
use std::fs;
use std::path::Path;
use crate::engine_monolith::MonolithEngine;

#[derive(Debug, Serialize, Deserialize)]
pub struct JarSummaryResponse {
    pub name: String,
    pub size: String,
    pub size_bytes: u64,
    pub classes: usize,
    pub packages: usize,
    pub java: String,
    pub plugin_name: Option<String>,
    pub plugin_author: Option<String>,
    pub error: Option<String>,
}

#[tauri::command]
pub fn get_gui_version() -> String {
    "2.0.2".to_string()
}

#[tauri::command]
pub fn scan_jar(jar_path: String) -> Result<serde_json::Value, String> {
    let engine = MonolithEngine::new();
    engine.scan_jar_inproc(&jar_path)
}

#[tauri::command]
pub fn jar_summary(jar_path: String) -> Result<JarSummaryResponse, String> {
    let p = Path::new(&jar_path);
    if !p.exists() {
        return Err("File not found".to_string());
    }

    let meta = fs::metadata(p).map_err(|e| e.to_string())?;
    let engine = MonolithEngine::new();

    let scan = engine.scan_jar_inproc(&jar_path).unwrap_or_default();
    let class_count = scan.get("total_classes").and_then(|v| v.as_u64()).unwrap_or(0) as usize;

    Ok(JarSummaryResponse {
        name: p.file_name().and_then(|s| s.to_str()).unwrap_or("unknown.jar").to_string(),
        size: format!("{:.2} MB", meta.len() as f64 / (1024.0 * 1024.0)),
        size_bytes: meta.len(),
        classes: class_count,
        packages: 1,
        java: "Java 21 (class 65.0)".to_string(),
        plugin_name: None,
        plugin_author: None,
        error: None,
    })
}

#[tauri::command]
pub fn open_path(target: String) -> Result<(), String> {
    open::that(&target).map_err(|e| e.to_string())
}

#[tauri::command]
pub fn open_external(url: String) -> Result<(), String> {
    open::that(&url).map_err(|e| e.to_string())
}

#[tauri::command]
pub fn read_file_content(path: String) -> Result<String, String> {
    let p = Path::new(&path);
    if !p.exists() {
        return Err("File not found".to_string());
    }
    fs::read_to_string(p).map_err(|e| e.to_string())
}
