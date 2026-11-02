import os
import sys
import json
import zipfile
import argparse
from datetime import datetime, timezone

def create_ndota_package(out_path, meta, file_mappings):
    print(f"Creating OTA package: {os.path.basename(out_path)}")
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with zipfile.ZipFile(out_path, 'w', compression=zipfile.ZIP_DEFLATED) as zf:
        zf.writestr("ndota_meta.json", json.dumps(meta, indent=2))
        count = 0
        for src_path, arc_name in file_mappings:
            if os.path.exists(src_path) and os.path.isfile(src_path):
                # Исключаем исходные карты .map и бинарники из OTA
                if src_path.endswith(".map") or arc_name.startswith("bin/"):
                    continue
                zf.write(src_path, arc_name)
                count += 1
        print(f"  + Added {count} files -> Total size: {os.path.getsize(out_path):,} bytes")

def collect_tree(src_dir, base_prefix=""):
    files = []
    if os.path.exists(src_dir):
        for root, _, filenames in os.walk(src_dir):
            for f in filenames:
                if f.endswith(".map") or f.endswith(".otatmp") or f.endswith(".bak"):
                    continue
                full = os.path.join(root, f)
                rel = os.path.relpath(full, src_dir).replace("\\", "/")
                arc_name = f"{base_prefix}/{rel}".lstrip("/") if base_prefix else rel
                files.append((full, arc_name))
    return files

def main():
    parser = argparse.ArgumentParser(description="Package NanoDecompiler OTA (.ndota) bundles")
    parser.add_argument("--tag", default="v2.0.7", help="Release tag e.g. v2.0.7")
    parser.add_argument("--release-dir", default="release-files", help="Release directory")
    args = parser.parse_args()

    tag = args.tag
    ver = tag.lstrip("v")
    rel_dir = args.release_dir
    os.makedirs(rel_dir, exist_ok=True)

    now_iso = datetime.now(timezone.utc).isoformat()

    # 1. WebUI OTA .ndota (чистый Web/SPA фронтенд ~205 КБ)
    webui_out = os.path.join(rel_dir, f"NanoDecompiler-OTA-WebUI-{tag}.ndota")
    webui_files = collect_tree("dist")
    if not webui_files:
        webui_files = collect_tree("mobile-android/assets")
    create_ndota_package(webui_out, {
        "packageName": "NanoDecompiler-OTA-WebUI",
        "format": "ndota-bundle-v1",
        "version": ver,
        "releaseTag": tag,
        "platform": "webui",
        "type": "gui_webui",
        "createdAt": now_iso
    }, webui_files)

    # 2. Electron OTA .ndota (Renderer + Main/Preload scripts ~290 КБ)
    electron_out = os.path.join(rel_dir, f"NanoDecompiler-OTA-Electron-{tag}.ndota")
    electron_files = collect_tree("dist", "dist") + collect_tree("dist-electron", "dist-electron")
    create_ndota_package(electron_out, {
        "packageName": "NanoDecompiler-OTA-Electron",
        "format": "ndota-bundle-v1",
        "version": ver,
        "releaseTag": tag,
        "platform": "electron",
        "type": "gui_electron",
        "createdAt": now_iso
    }, electron_files)

    # 3. Android Mobile OTA .ndota (чистые WebView ассеты ~205 КБ)
    android_out = os.path.join(rel_dir, f"NanoDecompiler-OTA-Android-{tag}.ndota")
    android_files = collect_tree("mobile-android/assets")
    create_ndota_package(android_out, {
        "packageName": "NanoDecompiler-OTA-Android",
        "format": "ndota-bundle-v1",
        "version": ver,
        "releaseTag": tag,
        "platform": "android",
        "type": "gui_android",
        "createdAt": now_iso
    }, android_files)

    # 4. Lite GUI OTA .ndota (Ассеты, иконки и темы C# Avalonia Lite ~31 КБ)
    lite_out = os.path.join(rel_dir, f"NanoDecompiler-OTA-Lite-{tag}.ndota")
    lite_files = collect_tree("src_lite_cs/NanoDecompilerLite/Assets", "Assets")
    create_ndota_package(lite_out, {
        "packageName": "NanoDecompiler-OTA-Lite",
        "format": "ndota-bundle-v1",
        "version": ver,
        "releaseTag": tag,
        "platform": "lite",
        "type": "gui_lite",
        "createdAt": now_iso
    }, lite_files)

    print("\nAll 4 OTA bundles generated cleanly without bloat!")

if __name__ == "__main__":
    main()
