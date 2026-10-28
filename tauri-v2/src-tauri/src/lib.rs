pub mod commands;
pub mod engine_monolith;

use commands::*;

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_opener::init())
        .invoke_handler(tauri::generate_handler![
            get_gui_version,
            scan_jar,
            jar_summary,
            open_path,
            open_external,
            read_file_content,
        ])
        .run(tauri::generate_context!())
        .expect("error while running NanoDecompiler Tauri application");
}
