#!/usr/bin/env python3
"""
scripts/build_android.py
Cross-platform builder for NanoDecompiler native Android app (ARM64).
Compiles resources with AAPT2, compiles Kotlin/Java sources with kotlinc,
dexes with D8, packages native libs and assets, and signs with Release Keystore.
"""

import os
import sys
import glob
import shutil
import zipfile
import subprocess
import argparse

def find_tool(candidates, name="tool"):
    for c in candidates:
        if c and os.path.isfile(c) and os.access(c, os.X_OK if not c.endswith('.jar') else os.R_OK):
            return c
    for c in candidates:
        if c and os.path.isfile(c):
            return c
    # Try finding in PATH
    which_res = shutil.which(name)
    if which_res:
        return which_res
    return None

def find_file(patterns):
    for p in patterns:
        matches = glob.glob(p)
        if matches:
            # Pick highest version or first match
            matches.sort(reverse=True)
            return matches[0]
    return None

def main():
    parser = argparse.ArgumentParser(description="Build NanoDecompiler Native Android APK")
    parser.add_argument("--version", default="v2.0.8", help="Version tag, e.g. v2.0.7")
    parser.add_argument("--out-dir", default=".", help="Directory to save final APK")
    parser.add_argument("--keystore", default=None, help="Path to release keystore")
    parser.add_argument("--keystore-pass", default="regi12345.", help="Keystore password")
    parser.add_argument("--key-alias", default="nanodev", help="Key alias")
    args = parser.parse_args()

    project_root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    mobile_dir = os.path.join(project_root, "mobile-android")
    os.chdir(project_root)

    print("=======================================================")
    print(f"Building NanoDecompiler Native Android APK ({args.version})")
    print(f"Project root: {project_root}")
    print(f"Mobile dir:   {mobile_dir}")
    print("=======================================================")

    # 1. Resolve Android SDK / Toolchain paths
    sdk_root = os.environ.get("ANDROID_SDK_ROOT") or os.environ.get("ANDROID_HOME") or ""
    local_toolchain = r"C:\Users\nanodev\.gemini\android-toolchain"
    user_keystore = r"C:\Users\nanodev\Desktop\ZL1_Secrets\nanodecompiler-release.keystore"

    aapt2 = find_tool([
        os.path.join(local_toolchain, "aapt2.exe"),
        os.path.join(local_toolchain, "aapt2"),
        find_file([os.path.join(sdk_root, "build-tools", "*", "aapt2*")]),
    ], "aapt2")

    android_jar = find_file([
        os.path.join(local_toolchain, "android.jar"),
        os.path.join(sdk_root, "platforms", "*", "android.jar"),
    ])

    d8_or_r8 = find_file([
        os.path.join(local_toolchain, "r8.jar"),
        os.path.join(local_toolchain, "d8.jar"),
        find_file([os.path.join(sdk_root, "build-tools", "*", "lib", "d8.jar")]),
        find_file([os.path.join(sdk_root, "build-tools", "*", "d8*")]),
    ])

    kotlinc = find_tool([
        os.path.join(local_toolchain, "kotlinc", "bin", "kotlinc.bat"),
        os.path.join(local_toolchain, "kotlinc", "bin", "kotlinc"),
        os.path.join(project_root, "kotlinc", "bin", "kotlinc"),
        shutil.which("kotlinc"),
    ], "kotlinc")

    kotlin_stdlib = None
    if kotlinc:
        kotlinc_dir = os.path.dirname(os.path.dirname(kotlinc))
        candidate_stdlib = os.path.join(kotlinc_dir, "lib", "kotlin-stdlib.jar")
        if os.path.isfile(candidate_stdlib):
            kotlin_stdlib = candidate_stdlib

    if not kotlin_stdlib:
        kotlin_stdlib = find_file([
            os.path.join(local_toolchain, "kotlinc", "lib", "kotlin-stdlib.jar"),
            os.path.join(project_root, "kotlinc", "lib", "kotlin-stdlib.jar"),
            find_file(["/usr/share/kotlin/lib/kotlin-stdlib.jar"]),
        ])

    signer_jar = find_file([
        os.path.join(local_toolchain, "uber-apk-signer.jar"),
        os.path.join(project_root, "uber-apk-signer*.jar"),
        find_file(["uber-apk-signer*.jar"]),
    ])

    keystore_path = args.keystore or (user_keystore if os.path.isfile(user_keystore) else None)
    if not keystore_path and os.path.isfile("release.keystore"):
        keystore_path = "release.keystore"

    print("Resolved toolchain:")
    print(f"  - AAPT2:         {aapt2}")
    print(f"  - android.jar:   {android_jar}")
    print(f"  - D8/R8:         {d8_or_r8}")
    print(f"  - Kotlinc:       {kotlinc}")
    print(f"  - Kotlin Stdlib: {kotlin_stdlib}")
    print(f"  - Signer JAR:    {signer_jar}")
    print(f"  - Keystore:      {keystore_path}")

    assert aapt2, "AAPT2 not found!"
    assert android_jar, "android.jar not found!"
    assert d8_or_r8, "D8 / R8 not found!"
    assert kotlinc, "kotlinc not found!"
    assert kotlin_stdlib, "kotlin-stdlib.jar not found!"

    # 2. Prepare build workspace
    compiled_res = os.path.join(mobile_dir, "compiled_res.zip")
    gen_dir = os.path.join(mobile_dir, "gen")
    bin_dir = os.path.join(mobile_dir, "bin")
    dex_dir = os.path.join(mobile_dir, "dex")
    temp_out_dir = os.path.join(mobile_dir, "output")
    unaligned_apk = os.path.join(mobile_dir, "unaligned.apk")

    for d in [gen_dir, bin_dir, dex_dir, temp_out_dir]:
        if os.path.exists(d):
            shutil.rmtree(d, ignore_errors=True)
        os.makedirs(d, exist_ok=True)

    if os.path.exists(unaligned_apk):
        os.remove(unaligned_apk)

    # 3. AAPT2 compile
    print("\n[1/6] Compiling Android resources with AAPT2 ...")
    cmd1 = [aapt2, "compile", "--dir", os.path.join(mobile_dir, "res"), "-o", compiled_res]
    subprocess.run(cmd1, check=True)

    # 4. AAPT2 link
    print("[2/6] Linking APK and generating R.java with AAPT2 ...")
    cmd2 = [
        aapt2, "link",
        "-I", android_jar,
        "--manifest", os.path.join(mobile_dir, "AndroidManifest.xml"),
        "--java", gen_dir,
        "-o", unaligned_apk,
        "-A", os.path.join(mobile_dir, "assets"),
        compiled_res
    ]
    subprocess.run(cmd2, check=True)

    # 5. Kotlinc compile
    print("[3/6] Compiling Kotlin & Java sources with kotlinc ...")
    src_dir = os.path.join(mobile_dir, "src", "com", "nanodev", "nanodecompiler")
    kt_sources = [
        os.path.join(gen_dir, "com", "nanodev", "nanodecompiler", "R.java"),
        os.path.join(src_dir, "FastScanEngine.kt"),
        os.path.join(src_dir, "DecompilerEngine.kt"),
        os.path.join(src_dir, "MainActivity.kt"),
        os.path.join(src_dir, "NanoFileProvider.kt"),
    ]
    kotlinc_cmd = [
        kotlinc,
        "-cp", android_jar,
        "-jvm-target", "1.8",
        "-d", bin_dir,
    ] + kt_sources

    # On Windows, kotlinc is often a bat file, needs shell=True or call via cmd
    use_shell = os.name == 'nt' and kotlinc.endswith('.bat')
    subprocess.run(kotlinc_cmd if not use_shell else subprocess.list2cmdline(kotlinc_cmd), shell=use_shell, check=True)

    # 6. D8 Dexing
    print("[4/6] Converting Kotlin classes to DEX with D8 ...")
    class_files = []
    for root, _, files in os.walk(bin_dir):
        for f in files:
            if f.endswith(".class"):
                class_files.append(os.path.join(root, f))

    if d8_or_r8.endswith(".jar"):
        cmd4 = [
            "java", "-cp", d8_or_r8, "com.android.tools.r8.D8",
            "--min-api", "24",
            "--lib", android_jar,
            "--output", dex_dir
        ] + class_files + [kotlin_stdlib]
    else:
        cmd4 = [
            d8_or_r8,
            "--min-api", "24",
            "--lib", android_jar,
            "--output", dex_dir
        ] + class_files + [kotlin_stdlib]
    subprocess.run(cmd4, check=True)

    # 7. Package DEX files & Native Libraries into APK
    print("[5/6] Packaging DEX and ARM64 native libraries into APK ...")
    dex_files = [f for f in os.listdir(dex_dir) if f.endswith(".dex")]
    assert len(dex_files) > 0, "No DEX files generated!"

    lib_dir = os.path.join(mobile_dir, "lib")
    with zipfile.ZipFile(unaligned_apk, 'a', compression=zipfile.ZIP_DEFLATED) as apk_zip:
        for df in dex_files:
            dex_path = os.path.join(dex_dir, df)
            apk_zip.write(dex_path, df)
            print(f"  + Added DEX: {df} ({os.path.getsize(dex_path):,} bytes)")

        if os.path.exists(lib_dir):
            for root, _, files in os.walk(lib_dir):
                for f in files:
                    full_path = os.path.join(root, f)
                    rel_path = os.path.relpath(full_path, mobile_dir).replace('\\', '/')
                    apk_zip.write(full_path, rel_path)
                    print(f"  + Added Native Lib: {rel_path} ({os.path.getsize(full_path):,} bytes)")

    # 8. Sign APK
    print("[6/6] Signing APK with Keystore ...")
    if signer_jar and os.path.isfile(signer_jar):
        if keystore_path and os.path.isfile(keystore_path):
            cmd6 = [
                "java", "-jar", signer_jar,
                "-a", unaligned_apk,
                "--ks", keystore_path,
                "--ksAlias", args.key_alias,
                "--ksPass", args.keystore_pass,
                "--ksKeyPass", args.keystore_pass,
                "-o", temp_out_dir
            ]
        else:
            print("[!] Release keystore not found, signing with debug key ...")
            cmd6 = ["java", "-jar", signer_jar, "-a", unaligned_apk, "-o", temp_out_dir]
        subprocess.run(cmd6, check=True)
        final_candidates = [os.path.join(temp_out_dir, f) for f in os.listdir(temp_out_dir) if f.endswith(".apk") and "unaligned" in f]
        signed_apk = final_candidates[0]
    else:
        # Fallback to apksigner if available
        apksigner = shutil.which("apksigner")
        signed_apk = unaligned_apk
        if apksigner and keystore_path and os.path.isfile(keystore_path):
            cmd6 = [
                apksigner, "sign",
                "--ks", keystore_path,
                "--ks-key-alias", args.key_alias,
                "--ks-pass", f"pass:{args.keystore_pass}",
                "--key-pass", f"pass:{args.keystore_pass}",
                signed_apk
            ]
            subprocess.run(cmd6, check=True)

    # 9. Copy to final output location
    os.makedirs(args.out_dir, exist_ok=True)
    target_apk = os.path.join(args.out_dir, f"NanoDecompiler-Android-arm64-{args.version}.apk")
    shutil.copy2(signed_apk, target_apk)
    size_mb = os.path.getsize(target_apk) / (1024 * 1024)

    print("\n=======================================================")
    print(f"SUCCESS! Native Android APK created: {target_apk} ({size_mb:.2f} MB)")
    print("=======================================================")

if __name__ == "__main__":
    main()
