use tauri::{AppHandle, Manager, WebviewUrl, WebviewWindowBuilder};

pub fn open_developer_log_window(app: &AppHandle) -> Result<(), String> {
    if let Some(existing) = app.get_webview_window("logs") {
        let _ = existing.show();
        let _ = existing.set_focus();
        return Ok(());
    }

    WebviewWindowBuilder::new(
        app,
        "logs",
        WebviewUrl::App("#/logs".into()),
    )
    .title("NanoDecompiler — Журнал разработчика")
    .inner_size(940.0, 640.0)
    .min_inner_size(700.0, 480.0)
    .resizable(true)
    .decorations(true)
    .build()
    .map_err(|e| format!("Не удалось создать окно логов: {}", e))?;

    Ok(())
}
