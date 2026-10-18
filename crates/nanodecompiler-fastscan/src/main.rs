use std::env;
use std::process;

fn main() {
    let args: Vec<String> = env::args().skip(1).collect();
    let is_json = args.iter().any(|a| a == "--json");
    // Первый аргумент, не являющийся флагом (раньше путём считался args[1] - при
    // `nanofastscan --json file.jar` путём становился сам "--json").
    let jar_path = match args.iter().find(|a| !a.starts_with("--")) {
        Some(p) => p,
        None => {
            eprintln!("Usage: nanofastscan <path_to_jar> [--json]");
            process::exit(1);
        }
    };

    match nanodecompiler_fastscan::scan_jar(jar_path) {
        Ok(report) => {
            if report.is_nanoguard_protected {
                if is_json {
                    println!("{}", serde_json::to_string_pretty(&report).unwrap());
                } else {
                    eprintln!("Файл защищен NanoGuard, декомпиляция невозможна!");
                }
                process::exit(78); // Специальный неразмеченный код выхода: защита NanoGuard
            }
            if is_json {
                match serde_json::to_string_pretty(&report) {
                    Ok(s) => println!("{}", s),
                    Err(e) => {
                        eprintln!("Error serializing report: {}", e);
                        process::exit(1);
                    }
                }
            } else {
                println!("=== NanoFastScan Report (Rust Core) ===");
                println!("JAR: {}", report.jar_name);
                println!("Size: {} KB", report.file_size_bytes / 1024);
                println!("Classes: {}", report.total_classes);
                println!("Packages: {}", report.total_packages);
                println!("Plugin: {}", report.plugin_name.as_deref().unwrap_or("Unknown"));
                println!("Main Class: {}", report.main_class.as_deref().unwrap_or("Unknown"));
                println!("Max Entropy: {:.2} / 8.0", report.max_entropy);
                println!("Danger Score: {} / 100", report.danger_score);
                if !report.threats.is_empty() {
                    println!("\n[!] Обнаружено угроз: {}", report.threats.len());
                    for t in &report.threats {
                        println!(" - [{}] {} in {}", t.severity, t.description, t.location);
                    }
                } else {
                    println!("\n[OK] Вредоносных конструкций не обнаружено.");
                }
            }
        }
        Err(e) => {
            eprintln!("Error scanning jar: {}", e);
            process::exit(1);
        }
    }
}
