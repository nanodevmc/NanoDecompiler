use serde::{Deserialize, Serialize};
use std::fs::File;
use std::io::Read;
use std::path::Path;
use std::process::Command;
use sha2::{Digest, Sha256};

#[derive(Debug, Serialize, Deserialize)]
pub struct GithubSearchResultItem {
    pub name: String,
    pub full_name: String,
    pub url: String,
    pub description: Option<String>,
    pub stars: u32,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct GithubSearchResponse {
    pub ok: bool,
    pub error: Option<String>,
    pub results: Option<Vec<GithubSearchResultItem>>,
}

pub struct NetworkService;

impl NetworkService {
    /// Поиск схожих репозиториев на GitHub
    pub fn search_github(plugin_name: Option<String>, author: Option<String>) -> GithubSearchResponse {
        let query = match (plugin_name, author) {
            (Some(p), Some(a)) => format!("{} {} in:name,description", p, a),
            (Some(p), None) => format!("{} in:name,description", p),
            (None, Some(a)) => format!("user:{}", a),
            (None, None) => {
                return GithubSearchResponse {
                    ok: false,
                    error: Some("Не указаны параметры поиска".to_string()),
                    results: None,
                };
            }
        };

        let encoded = query.replace(' ', "%20");
        let url = format!(
            "https://api.github.com/search/repositories?q={}&sort=stars&order=desc&per_page=5",
            encoded
        );

        let curl_cmd = if cfg!(target_os = "windows") { "curl.exe" } else { "curl" };
        let output = match Command::new(curl_cmd)
            .arg("-s")
            .arg("-H")
            .arg("User-Agent: NanoDecompiler-Tauri")
            .arg("-H")
            .arg("Accept: application/vnd.github.v3+json")
            .arg(&url)
            .output()
        {
            Ok(o) => o,
            Err(e) => {
                return GithubSearchResponse {
                    ok: false,
                    error: Some(format!("Ошибка сетевого запроса: {}", e)),
                    results: None,
                };
            }
        };

        if !output.status.success() {
            return GithubSearchResponse {
                ok: false,
                error: Some("Не удалось получить ответ от GitHub API".to_string()),
                results: None,
            };
        }

        let body = String::from_utf8_lossy(&output.stdout);
        let val: serde_json::Value = match serde_json::from_str(&body) {
            Ok(v) => v,
            Err(e) => {
                return GithubSearchResponse {
                    ok: false,
                    error: Some(format!("Ошибка парсинга JSON: {}", e)),
                    results: None,
                };
            }
        };

        let items = val.get("items").and_then(|i| i.as_array());
        let mut results = Vec::new();

        if let Some(list) = items {
            for item in list {
                let name = item.get("name").and_then(|v| v.as_str()).unwrap_or_default().to_string();
                let full_name = item.get("full_name").and_then(|v| v.as_str()).unwrap_or_default().to_string();
                let html_url = item.get("html_url").and_then(|v| v.as_str()).unwrap_or_default().to_string();
                let desc = item.get("description").and_then(|v| v.as_str()).map(|s| s.to_string());
                let stars = item.get("stargazers_count").and_then(|v| v.as_u64()).unwrap_or(0) as u32;

                results.push(GithubSearchResultItem {
                    name,
                    full_name,
                    url: html_url,
                    description: desc,
                    stars,
                });
            }
        }

        GithubSearchResponse {
            ok: true,
            error: None,
            results: Some(results),
        }
    }

    /// Потоковое скачивание файла модуля с верификацией SHA-256
    pub fn download_with_sha256_verify(url: &str, dest_path: &Path, expected_sha256: &str) -> Result<(), String> {
        let temp_path = dest_path.with_extension("tmp_download");

        let curl_cmd = if cfg!(target_os = "windows") { "curl.exe" } else { "curl" };
        let status = Command::new(curl_cmd)
            .arg("-L")
            .arg("-s")
            .arg("-o")
            .arg(&temp_path)
            .arg(url)
            .status()
            .map_err(|e| format!("Ошибка запуска curl: {}", e))?;

        if !status.success() {
            let _ = std::fs::remove_file(&temp_path);
            return Err("Ошибка при скачивании модуля".to_string());
        }

        // Проверка SHA-256
        let mut f = File::open(&temp_path).map_err(|e| format!("Не удалось открыть файл: {}", e))?;
        let mut hasher = Sha256::new();
        let mut buffer = [0u8; 64 * 1024];

        loop {
            let n = f.read(&mut buffer).map_err(|e| format!("Ошибка чтения: {}", e))?;
            if n == 0 {
                break;
            }
            hasher.update(&buffer[..n]);
        }

        let actual_hash = format!("{:x}", hasher.finalize());
        if !expected_sha256.is_empty() && actual_hash.to_lowercase() != expected_sha256.to_lowercase() {
            let _ = std::fs::remove_file(&temp_path);
            return Err(format!(
                "Несовпадение контрольной суммы! Ожидалось: {}, получено: {}",
                expected_sha256, actual_hash
            ));
        }

        // Атомарное перемещение
        std::fs::rename(&temp_path, dest_path)
            .map_err(|e| format!("Ошибка финализации файла: {}", e))?;

        Ok(())
    }
}
