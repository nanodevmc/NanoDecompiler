import os
import sys
import json
import zipfile
import argparse
from datetime import datetime, timezone

def create_ndota_package(out_path, meta, file_mappings):
    print(f"Creating NDOTA GUI package: {os.path.basename(out_path)}")
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with zipfile.ZipFile(out_path, 'w', compression=zipfile.ZIP_DEFLATED) as zf:
        zf.writestr("ndota_meta.json", json.dumps(meta, indent=2))
        count = 0
        for src_path, arc_name in file_mappings:
            if os.path.exists(src_path) and os.path.isfile(src_path):
                zf.write(src_path, arc_name)
                count += 1
        print(f"  + Included {count} files -> Total size: {os.path.getsize(out_path):,} bytes")

def collect_tree(src_dir, base_prefix=""):
    files = []
    if os.path.exists(src_dir):
        for root, _, filenames in os.walk(src_dir):
            for f in filenames:
                full = os.path.join(root, f)
                rel = os.path.relpath(full, src_dir).replace("\\", "/")
                arc_name = f"{base_prefix}/{rel}".lstrip("/") if base_prefix else rel
                files.append((full, arc_name))
    return files

def main():
    parser = argparse.ArgumentParser(description="Package NanoDecompiler GUI OTA (.ndota) bundles")
    parser.add_argument("--tag", default="v2.0.7", help="Release tag e.g. v2.0.7")
    parser.add_argument("--release-dir", default="release-files", help="Release directory")
    args = parser.parse_args()

    tag = args.tag
    ver = tag.lstrip("v")
    rel_dir = args.release_dir
    os.makedirs(rel_dir, exist_ok=True)

    now_iso = datetime.now(timezone.utc).isoformat()

    # 1. WebUI .ndota (чистый Web/SPA фронтенд)
    webui_out = os.path.join(rel_dir, f"NanoDecompiler-GUI-WebUI-{tag}.ndota")
    webui_files = collect_tree("dist")
    if not webui_files:
        webui_files = collect_tree("mobile-android/assets")
    create_ndota_package(webui_out, {
        "packageName": "NanoDecompiler-GUI-WebUI",
        "format": "ndota-bundle-v1",
        "version": ver,
        "releaseTag": tag,
        "type": "gui_webui",
        "createdAt": now_iso
    }, webui_files)
    # Сохраняем также каноническое имя NanoDecompiler-WebUI-{tag}.ndota
    legacy_webui = os.path.join(rel_dir, f"NanoDecompiler-WebUI-{tag}.ndota")
    if os.path.exists(webui_out):
        import shutil
        shutil.copy2(webui_out, legacy_webui)

    # 2. Electron GUI .ndota (Renderer + Main/Preload scripts для горячего обновления Electron)
    electron_out = os.path.join(rel_dir, f"NanoDecompiler-GUI-Electron-{tag}.ndota")
    electron_files = collect_tree("dist", "dist") + collect_tree("dist-electron", "dist-electron")
    create_ndota_package(electron_out, {
        "packageName": "NanoDecompiler-GUI-Electron",
        "format": "ndota-bundle-v1",
        "version": ver,
        "releaseTag": tag,
        "type": "gui_electron",
        "createdAt": now_iso
    }, electron_files)

    # 3. Android Mobile GUI .ndota (WebView ассеты и интерфейс мобильного приложения)
    android_out = os.path.join(rel_dir, f"NanoDecompiler-GUI-Android-{tag}.ndota")
    android_files = collect_tree("mobile-android/assets")
    if not android_files:
        android_files = collect_tree("dist")
    create_ndota_package(android_out, {
        "packageName": "NanoDecompiler-GUI-Android",
        "format": "ndota-bundle-v1",
        "version": ver,
        "releaseTag": tag,
        "type": "gui_android",
        "createdAt": now_iso
    }, android_files)

    # 4. Lite GUI .ndota (Ассеты, иконки и темы C# Avalonia Lite клиента)
    lite_out = os.path.join(rel_dir, f"NanoDecompiler-GUI-Lite-{tag}.ndota")
    lite_files = collect_tree("src_lite_cs/NanoDecompilerLite/Assets", "Assets")
    create_ndota_package(lite_out, {
        "packageName": "NanoDecompiler-GUI-Lite",
        "format": "ndota-bundle-v1",
        "version": ver,
        "releaseTag": tag,
        "type": "gui_lite",
        "createdAt": now_iso
    }, lite_files)

    print("\nAll GUI .ndota bundles generated successfully (WebUI, Electron, Android, Lite)!")

if __name__ == "__main__":
    main()
