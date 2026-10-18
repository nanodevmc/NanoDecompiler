use std::fs::File;
use std::io::{BufReader, Read};
use std::path::Path;
use rayon::prelude::*;
use serde::{Deserialize, Serialize};
use zip::ZipArchive;

use crate::entropy::shannon_entropy;
use crate::malware::{MalwareScanner, ThreatFinding};

// Ограничения для НЕДОВЕРЕННОГО jar. Размеры в заголовках архива контролирует его автор,
// поэтому раньше `Vec::with_capacity(entry.size())` мог запросить до 4 ГиБ у крошечного
// файла (паника "capacity overflow"/аборт по памяти - а это FFI-функция, и паника через
// границу C-ABI - неопределённое поведение), а `read_to_end`/`read_to_string` читали запись
// без верхней границы (zip-bomb).
const MAX_ENTRIES: usize = 500_000;
const MAX_CLASS_BYTES: u64 = 64 * 1024 * 1024;
const MAX_TOTAL_CLASS_BYTES: u64 = 1024 * 1024 * 1024;
const MAX_PLUGIN_YML_BYTES: u64 = 1024 * 1024;

#[derive(Debug, Serialize, Deserialize)]
pub struct JarScanReport {
    pub jar_name: String,
    pub file_size_bytes: u64,
    pub total_classes: usize,
    pub total_packages: usize,
    pub plugin_name: Option<String>,
    pub main_class: Option<String>,
    pub api_version: Option<String>,
    pub danger_score: u32, // 0..100
    pub max_entropy: f64,
    pub threats: Vec<ThreatFinding>,
    pub is_nanoguard_protected: bool,
    pub guard_code: Option<u32>,
}

pub fn scan_jar<P: AsRef<Path>>(path: P) -> Result<JarScanReport, String> {
    let p = path.as_ref();
    let file = File::open(p).map_err(|e| format!("Failed to open jar: {}", e))?;
    let metadata = file.metadata().map_err(|e| format!("Failed to read metadata: {}", e))?;
    let file_size = metadata.len();

    // Раньше файл отображался в память (memmap2 + unsafe): если jar изменяли или обрезали
    // во время сканирования, чтение отображённой области давало SIGBUS/краш всего процесса.
    // Обычное буферизованное чтение безопасно, а скорость определяется разбором классов
    // (параллельным, ниже), а не чтением файла.
    let reader = BufReader::new(file);
    let mut zip = ZipArchive::new(reader).map_err(|e| format!("Bad zip format: {}", e))?;
    if zip.len() > MAX_ENTRIES {
        return Err(format!("Too many entries in archive: {}", zip.len()));
    }

    let mut class_entries: Vec<(String, Vec<u8>)> = Vec::new();
    let mut plugin_yml_content: Option<String> = None;
    let mut is_nanoguard_protected = false;
    let mut total_class_bytes: u64 = 0;

    for i in 0..zip.len() {
        if let Ok(mut entry) = zip.by_index(i) {
            // zip >= 9: name() возвращает ZipResult<Cow<str>> (раньше &str) - запись с нечитаемым
            // именем пропускаем. На zip 9.0 прежняя строка `entry.name().to_string()` не компилировалась.
            let name = match entry.name() {
                Ok(n) => n.into_owned(),
                Err(_) => continue,
            };
            let upper = name.to_uppercase();
            if upper == "NANOGUARD.INF" || upper.ends_with("/NANOGUARD.INF") || upper.contains("NANOGUARD.INF") {
                is_nanoguard_protected = true;
            }
            if name == "plugin.yml" {
                let mut s = String::new();
                let _ = (&mut entry).take(MAX_PLUGIN_YML_BYTES).read_to_string(&mut s);
                plugin_yml_content = Some(s);
            } else if name.ends_with(".class") && !name.contains("module-info") {
                if total_class_bytes >= MAX_TOTAL_CLASS_BYTES {
                    continue;
                }
                let mut buf = Vec::new();
                let _ = (&mut entry).take(MAX_CLASS_BYTES).read_to_end(&mut buf);
                total_class_bytes += buf.len() as u64;
                class_entries.push((name, buf));
            }
        }
    }

    let total_classes = class_entries.len();
    let mut packages = std::collections::HashSet::new();
    for (name, _) in &class_entries {
        if let Some(pos) = name.rfind('/') {
            packages.insert(name[..pos].to_string());
        }
    }

    // Параллельный анализ классов через Rayon
    let results: Vec<(Vec<ThreatFinding>, f64)> = class_entries
        .par_iter()
        .map(|(name, bytes)| {
            let entropy = shannon_entropy(bytes);
            let threats = MalwareScanner::scan_class_bytes(name, bytes);
            (threats, entropy)
        })
        .collect();

    let mut all_threats = Vec::new();
    let mut max_entropy: f64 = 0.0;
    for (threats, entropy) in results {
        all_threats.extend(threats);
        if entropy > max_entropy {
            max_entropy = entropy;
        }
    }

    let mut danger_score = 0u32;
    for t in &all_threats {
        match t.severity.as_str() {
            "CRITICAL" => danger_score = danger_score.saturating_add(35),
            "HIGH" => danger_score = danger_score.saturating_add(20),
            "MEDIUM" => danger_score = danger_score.saturating_add(10),
            _ => danger_score = danger_score.saturating_add(5),
        }
    }
    if max_entropy > 7.4 {
        danger_score = danger_score.saturating_add(15);
    }
    danger_score = danger_score.min(100);

    let (plugin_name, main_class, api_version) = parse_plugin_yml(plugin_yml_content.as_deref());

    Ok(JarScanReport {
        jar_name: p.file_name().unwrap_or_default().to_string_lossy().to_string(),
        file_size_bytes: file_size,
        total_classes,
        total_packages: packages.len(),
        plugin_name,
        main_class,
        api_version,
        danger_score,
        max_entropy,
        threats: all_threats,
        is_nanoguard_protected,
        guard_code: if is_nanoguard_protected { Some(78) } else { None },
    })
}

fn unquote(value: &str) -> String {
    value.trim().trim_matches('\'').trim_matches('"').to_string()
}

fn parse_plugin_yml(content: Option<&str>) -> (Option<String>, Option<String>, Option<String>) {
    let mut name = None;
    let mut main = None;
    let mut api_ver = None;

    if let Some(text) = content {
        for line in text.lines() {
            // Только ключи верхнего уровня (строки без отступа): раньше строка
            // обрезалась по краям, и `name:`/`main:` из вложенных секций (commands,
            // permissions) перезаписывали настоящие значения.
            let line = line.trim_end();
            if name.is_none() && line.starts_with("name:") {
                name = Some(unquote(&line["name:".len()..]));
            } else if main.is_none() && line.starts_with("main:") {
                main = Some(unquote(&line["main:".len()..]));
            } else if api_ver.is_none() && line.starts_with("api-version:") {
                api_ver = Some(unquote(&line["api-version:".len()..]));
            }
        }
    }
    (name, main, api_ver)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn plugin_yml_ignores_nested_keys() {
        let yml = "name: Real\nmain: a.b.C\ncommands:\n  x:\n    name: Fake\n    main: Fake\napi-version: '1.20'\n";
        let (n, m, a) = parse_plugin_yml(Some(yml));
        assert_eq!(n.as_deref(), Some("Real"));
        assert_eq!(m.as_deref(), Some("a.b.C"));
        assert_eq!(a.as_deref(), Some("1.20"));
    }
}
