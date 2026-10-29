import os
import glob
import subprocess

NDK_BIN = r"C:\Users\nanodev\.gemini\android-toolchain\ndk\android-ndk-r26c\toolchains\llvm\prebuilt\windows-x86_64\bin"
CLANG_PP = os.path.join(NDK_BIN, "aarch64-linux-android28-clang++.cmd")

PROJECT_DIR = r"C:\Users\nanodev\.gemini\antigravity\scratch\NanoDecompiler"
ENGINE_DIR = os.path.join(PROJECT_DIR, "resources", "engine_cpp")
SRC_DIR = os.path.join(ENGINE_DIR, "src")
INCLUDE_DIR = os.path.join(ENGINE_DIR, "include")
BUILD_DIR = os.path.join(ENGINE_DIR, "build_android")

os.makedirs(BUILD_DIR, exist_ok=True)

# Исходники без бэкапов
cpp_files = [f for f in glob.glob(os.path.join(SRC_DIR, "*.cpp")) if not f.endswith(".bak")]

print(f"Compiling {len(cpp_files)} C++ engine sources for aarch64-linux-android with -static-libstdc++ ...")

out_bin = os.path.join(BUILD_DIR, "NanoDecompilerCLI")

# Компиляция со статической линковкой C++ stdlib для гарантированной работы на всех версиях Android
cmd = [
    CLANG_PP,
    "-std=c++17",
    "-O2",
    "-fPIE",
    "-pie",
    "-static-libstdc++",
    "-ffunction-sections",
    "-fdata-sections",
    "-I" + INCLUDE_DIR,
    "-o", out_bin,
] + cpp_files + [
    "-lz",
    "-llog",
    "-Wl,--gc-sections",
    "-s"
]

print("Running Clang++ with -static-libstdc++ ...")
subprocess.run(cmd, check=True)

print("NanoDecompilerCLI size:", os.path.getsize(out_bin), "bytes")
print("C++ Engine build SUCCESS!")
