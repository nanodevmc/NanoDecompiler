use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::path::{Path, PathBuf};
use std::process::Command;

#[derive(Debug, Serialize, Deserialize)]
pub struct OpenResult {
    pub ok: bool,
    pub error: Option<String>,
}

trait EditorDetector: Send + Sync {
    fn name(&self) -> &'static str;
    fn detect(&self) -> Option<PathBuf>;
}

struct VsCodeDetector;
impl EditorDetector for VsCodeDetector {
    fn name(&self) -> &'static str {
        "vscode"
    }

    fn detect(&self) -> Option<PathBuf> {
        let local_app_data = std::env::var("LOCALAPPDATA").unwrap_or_default();
        let program_files = std::env::var("ProgramFiles").unwrap_or_default();

        let candidates = [
            PathBuf::from(&local_app_data).join(r"Programs\Microsoft VS Code\Code.exe"),
            PathBuf::from(&program_files).join(r"Microsoft VS Code\Code.exe"),
            PathBuf::from(r"C:\Program Files\Microsoft VS Code\Code.exe"),
        ];

        candidates.into_iter().find(|p| p.exists())
    }
}

struct IdeaDetector;
impl EditorDetector for IdeaDetector {
    fn name(&self) -> &'static str {
        "idea"
    }

    fn detect(&self) -> Option<PathBuf> {
        let local_app_data = std::env::var("LOCALAPPDATA").unwrap_or_default();
        let program_files = std::env::var("ProgramFiles").unwrap_or_default();

        let candidates = [
            PathBuf::from(&local_app_data).join(r"Programs\IntelliJ IDEA Ultimate\bin\idea64.exe"),
            PathBuf::from(&local_app_data).join(r"Programs\IntelliJ IDEA Community Edition\bin\idea64.exe"),
            PathBuf::from(&program_files).join(r"JetBrains\IntelliJ IDEA\bin\idea64.exe"),
        ];

        candidates.into_iter().find(|p| p.exists())
    }
}

struct NotepadPlusDetector;
impl EditorDetector for NotepadPlusDetector {
    fn name(&self) -> &'static str {
        "notepadpp"
    }

    fn detect(&self) -> Option<PathBuf> {
        let program_files = std::env::var("ProgramFiles").unwrap_or_default();
        let program_files_x86 = std::env::var("ProgramFiles(x86)").unwrap_or_default();

        let candidates = [
            PathBuf::from(&program_files).join(r"Notepad++\notepad++.exe"),
            PathBuf::from(&program_files_x86).join(r"Notepad++\notepad++.exe"),
        ];

        candidates.into_iter().find(|p| p.exists())
    }
}

pub struct EditorService {
    detectors: Vec<Box<dyn EditorDetector>>,
}

impl EditorService {
    pub fn new() -> Self {
        Self {
            detectors: vec![
                Box::new(VsCodeDetector),
                Box::new(IdeaDetector),
                Box::new(NotepadPlusDetector),
            ],
        }
    }

    pub fn detect_all(&self) -> HashMap<String, bool> {
        let mut map = HashMap::new();
        for d in &self.detectors {
            map.insert(d.name().to_string(), d.detect().is_some());
        }
        map
    }

    pub fn open_with(&self, editor_id: &str, target_path: &str) -> OpenResult {
        let target = Path::new(target_path);
        if !target.exists() {
            return OpenResult {
                ok: false,
                error: Some("Целевой файл или папка не существует".to_string()),
            };
        }

        let detector = self.detectors.iter().find(|d| d.name() == editor_id);
        let exe = match detector.and_then(|d| d.detect()) {
            Some(path) => path,
            None => {
                return OpenResult {
                    ok: false,
                    error: Some(format!("Редактор '{}' не найден в системе", editor_id)),
                };
            }
        };

        match Command::new(&exe).arg(target_path).spawn() {
            Ok(_) => OpenResult { ok: true, error: None },
            Err(e) => OpenResult {
                ok: false,
                error: Some(format!("Ошибка запуска {}: {}", editor_id, e)),
            },
        }
    }
}
