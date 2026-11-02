import os
import sys
import json
import zipfile
import argparse
from datetime import datetime, timezone

def create_ndota_package(out_path, meta, file_mappings):
    print(f"Creating NDOTA package: {os.path.basename(out_path)}")
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with zipfile.ZipFile(out_path, 'w', compression=zipfile.ZIP_DEFLATED) as zf:
        # Write metadata
        zf.writestr("ndota_meta.json", json.dumps(meta, indent=2))
        # Write included files
        for src_path, arc_name in file_mappings:
            if os.path.exists(src_path):
                zf.write(src_path, arc_name)
                print(f"  + {arc_name} ({os.path.getsize(src_path):,} bytes)")
            else:
                print(f"  - warning: {src_path} not found")
    print(f"  -> Total size: {os.path.getsize(out_path):,} bytes")

def main():
    parser = argparse.ArgumentParser(description="Package NanoDecompiler OTA (.ndota) bundles")
    parser.add_argument("--tag", default="v2.0.6", help="Release tag e.g. v2.0.7")
    parser.add_argument("--release-dir", default="release-files", help="Release directory")
    args = parser.parse_args()

    tag = args.tag
    ver = tag.lstrip("v")
    rel_dir = args.release_dir
    os.makedirs(rel_dir, exist_ok=True)

    now_iso = datetime.now(timezone.utc).isoformat()

    # 1. WebUI .ndota
    webui_out = os.path.join(rel_dir, f"NanoDecompiler-WebUI-{tag}.ndota")
    webui_files = []
    # Check dist or mobile-android assets
    ui_src = "dist" if os.path.exists("dist") else "mobile-android/assets"
    if os.path.exists(ui_src):
        for root, _, files in os.walk(ui_src):
            for f in files:
                full = os.path.join(root, f)
                rel = os.path.relpath(full, ui_src).replace("\\", "/")
                webui_files.append((full, rel))
    create_ndota_package(webui_out, {
        "packageName": "NanoDecompiler-WebUI",
        "format": "ndota-bundle-v1",
        "version": ver,
        "releaseTag": tag,
        "type": "gui_renderer",
        "createdAt": now_iso
    }, webui_files)

    # 2. Windows C++ Engine .ndota
    win_cli = os.path.join(rel_dir, f"NanoDecompilerCLI-Windows-x64-{tag}.exe")
    if not os.path.exists(win_cli):
        win_cli = "resources/engine/NanoDecompilerCLI.exe"
    if not os.path.exists(win_cli):
        win_cli = "resources/engine_cpp/build/NanoDecompilerCLI.exe"
    if os.path.exists(win_cli):
        engine_win_out = os.path.join(rel_dir, f"NanoDecompiler-Engine-Windows-x64-{tag}.ndota")
        create_ndota_package(engine_win_out, {
            "packageName": "NanoDecompiler-Engine-Windows-x64",
            "format": "ndota-bundle-v1",
            "version": ver,
            "releaseTag": tag,
            "type": "engine_binary",
            "targetOs": "windows",
            "targetArch": "x64",
            "createdAt": now_iso
        }, [(win_cli, "bin/NanoDecompilerCLI.exe")])

    # 3. Android ARM64 Engine .ndota
    android_lib_dir = "mobile-android/lib/arm64-v8a"
    if os.path.exists(android_lib_dir):
        android_ndota_out = os.path.join(rel_dir, f"NanoDecompiler-Engine-Android-arm64-{tag}.ndota")
        android_files = []
        for f in os.listdir(android_lib_dir):
            full = os.path.join(android_lib_dir, f)
            if os.path.isfile(full):
                android_files.append((full, f"lib/arm64-v8a/{f}"))
        create_ndota_package(android_ndota_out, {
            "packageName": "NanoDecompiler-Engine-Android-arm64",
            "format": "ndota-bundle-v1",
            "version": ver,
            "releaseTag": tag,
            "type": "engine_native_library",
            "targetOs": "android",
            "targetArch": "arm64-v8a",
            "createdAt": now_iso
        }, android_files)

    print("\nAll .ndota bundles generated successfully!")

if __name__ == "__main__":
    main()
