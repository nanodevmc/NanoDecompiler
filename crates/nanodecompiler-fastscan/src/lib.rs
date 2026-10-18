pub mod entropy;
pub mod malware;
pub mod scanner;

use std::ffi::{CStr, CString};
use std::os::raw::c_char;
use std::panic::{catch_unwind, AssertUnwindSafe};

pub use scanner::{scan_jar, JarScanReport};

/// Ошибка в виде JSON. Раньше строка собиралась через format!() с заменой только `"`:
/// обратные слэши (пути Windows) и переводы строк давали НЕВАЛИДНЫЙ JSON.
fn error_json(message: &str) -> *mut c_char {
    let json = serde_json::json!({ "error": message }).to_string();
    match CString::new(json) {
        Ok(c) => c.into_raw(),
        Err(_) => std::ptr::null_mut(),
    }
}

fn scan_to_c_string(path: &str) -> *mut c_char {
    match scan_jar(path) {
        Ok(report) => match serde_json::to_string(&report) {
            Ok(json) => match CString::new(json) {
                Ok(c) => c.into_raw(),
                Err(_) => error_json("report contains an interior NUL byte"),
            },
            Err(e) => error_json(&format!("failed to serialize report: {}", e)),
        },
        Err(err) => error_json(&err),
    }
}

/// C-ABI интерфейс для вызова из C++ / Electron / C#
///
/// `jar_path` - указатель на NUL-терминированную UTF-8 строку. Возвращает указатель на
/// JSON-строку (отчёт либо `{"error": ...}`), которую ОБЯЗАТЕЛЬНО освободить через
/// `nanodecompiler_fastscan_free`; NULL - только при NULL/невалидном UTF-8 на входе.
#[no_mangle]
pub extern "C" fn nanodecompiler_fastscan_jar(jar_path: *const c_char) -> *mut c_char {
    if jar_path.is_null() {
        return std::ptr::null_mut();
    }
    // SAFETY: вызывающий передаёт валидную NUL-терминированную строку (контракт выше).
    let c_str = unsafe { CStr::from_ptr(jar_path) };
    let path_str = match c_str.to_str() {
        Ok(s) => s,
        Err(_) => return std::ptr::null_mut(),
    };

    // Паника не должна пересекать границу extern "C" (неопределённое поведение):
    // превращаем её в обычную JSON-ошибку.
    match catch_unwind(AssertUnwindSafe(|| scan_to_c_string(path_str))) {
        Ok(ptr) => ptr,
        Err(_) => error_json("internal error while scanning the archive"),
    }
}

/// Освобождает строку, возвращённую `nanodecompiler_fastscan_jar`. Передавать можно только
/// такие указатели (или NULL), и только один раз.
#[no_mangle]
pub extern "C" fn nanodecompiler_fastscan_free(ptr: *mut c_char) {
    if !ptr.is_null() {
        // SAFETY: ptr получен из CString::into_raw в этой же библиотеке (контракт выше).
        unsafe {
            let _ = CString::from_raw(ptr);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn error_json_escapes_backslashes_and_quotes() {
        let ptr = error_json("C:\\Users\\x \"y\"\nz");
        let s = unsafe { CStr::from_ptr(ptr) }.to_str().unwrap().to_string();
        nanodecompiler_fastscan_free(ptr);
        let v: serde_json::Value = serde_json::from_str(&s).expect("valid json");
        assert_eq!(v["error"], "C:\\Users\\x \"y\"\nz");
    }

    #[test]
    fn null_path_returns_null() {
        assert!(nanodecompiler_fastscan_jar(std::ptr::null()).is_null());
    }
}
