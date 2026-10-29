import urllib.request
import os
import zipfile
import sys
import time

URL = "https://dl.google.com/android/repository/android-ndk-r26c-windows.zip"
DEST_DIR = r"C:\Users\nanodev\.gemini\android-toolchain\ndk"
ZIP_PATH = r"C:\Users\nanodev\.gemini\android-toolchain\ndk.zip"

os.makedirs(r"C:\Users\nanodev\.gemini\android-toolchain", exist_ok=True)

if not os.path.exists(DEST_DIR):
    print("Starting download of Android NDK r26c (629 MB)...")
    req = urllib.request.Request(URL, headers={"User-Agent": "curl/7.68.0"})
    
    t0 = time.time()
    last_log = t0
    downloaded = 0
    
    with urllib.request.urlopen(req) as resp, open(ZIP_PATH, "wb") as f:
        total = int(resp.headers.get("Content-Length", 0))
        chunk_size = 1024 * 1024  # 1MB
        while True:
            chunk = resp.read(chunk_size)
            if not chunk:
                break
            f.write(chunk)
            downloaded += len(chunk)
            now = time.time()
            if now - last_log > 10:  # каждые 10 секунд
                pct = (downloaded / total * 100) if total else 0
                speed = downloaded / (now - t0) / (1024 * 1024)
                print(f"Downloaded: {downloaded / (1024*1024):.1f}/{total / (1024*1024):.1f} MB ({pct:.1f}%) @ {speed:.1f} MB/s", flush=True)
                last_log = now

    print(f"Download completed in {time.time() - t0:.1f}s. Extracting LLVM toolchain...")
    
    os.makedirs(DEST_DIR, exist_ok=True)
    with zipfile.ZipFile(ZIP_PATH, 'r') as z:
        # Распаковываем только llvm toolchain и sysroot
        members = [m for m in z.namelist() if "toolchains/llvm/prebuilt/windows-x86_64" in m or "source.properties" in m]
        print(f"Extracting {len(members)} toolchain files...")
        z.extractall(DEST_DIR, members)
        
    os.remove(ZIP_PATH)
    print("NDK LLVM Toolchain ready in:", DEST_DIR)
else:
    print("NDK already installed in:", DEST_DIR)
