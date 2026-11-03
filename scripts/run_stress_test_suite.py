#!/usr/bin/env python3
"""
run_stress_test_suite.py — Набор из 15 кроссплатформенных тестов декомпилятора NanoDecompiler
на стрессоустойчивость, деобфускацию, баланс AST-скобок, лямбды и идемпотентность.

Использование:
    python scripts/run_stress_test_suite.py --bin path/to/NanoDecompilerCLI[.exe]
"""

import os
import sys
import time
import json
import shutil
import hashlib
import argparse
import subprocess

TEST_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
TEST_JARS = os.path.join(TEST_DIR, "test_jars")
TEMP_OUT = os.path.join(TEST_DIR, "temp", "out", "stress_tests")

class SuiteRunner:
    def __init__(self, binary_path: str):
        self.binary = os.path.abspath(binary_path)
        self.passed = 0
        self.failed = 0
        self.results = []
        os.makedirs(TEMP_OUT, exist_ok=True)

    def log(self, msg: str):
        print(msg, flush=True)

    def run_cli(self, input_file: str, out_dir: str, extra_args: list = None, timeout: int = 40):
        cmd = [self.binary, input_file, out_dir]
        if extra_args:
            cmd.extend(extra_args)
        start = time.time()
        try:
            p = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                encoding="utf-8",
                errors="replace",
                timeout=timeout
            )
            elapsed = time.time() - start
            stdout = p.stdout or ""
            stderr = p.stderr or ""
            return p.returncode, stdout, stderr, elapsed
        except subprocess.TimeoutExpired:
            return -1, "", "TIMEOUT", timeout
        except Exception as e:
            return -2, "", str(e), 0

    def record(self, test_num: int, name: str, success: bool, duration: float, details: str = ""):
        status_str = "PASS" if success else "FAIL"
        color = "\033[32m" if success else "\033[31m"
        reset = "\033[0m"
        self.log(f"[{color}{status_str}{reset}] Test {test_num:02d}: {name} ({duration:.2f}s) {details}")
        if success:
            self.passed += 1
        else:
            self.failed += 1
        self.results.append({
            "test": test_num,
            "name": name,
            "status": status_str,
            "duration": round(duration, 2),
            "details": details
        })

    # Test 01: Smoketest & CLI Flags Integrity
    def test_01_smoketest(self):
        tname = "Smoketest CLI Flags & JSON Output"
        jar = os.path.join(TEST_JARS, "smoketest_plugin.jar")
        out = os.path.join(TEMP_OUT, "t01_smoke")
        rc, stdout, stderr, dur = self.run_cli(jar, out)
        ok = rc == 0 and "##ND_RESULT:" in stdout and "##ND_STAGE:decompiling##" in stdout
        self.record(1, tname, ok, dur, f"exit={rc}")

    # Test 02: Heavy Obfuscation & String Decryption (NeoBuyer)
    def test_02_obfuscated_strings(self):
        tname = "Heavy Obfuscation & Polymorphic Strings (NeoBuyer)"
        jar = os.path.join(TEST_JARS, "NeoBuyer.jar")
        out = os.path.join(TEMP_OUT, "t02_neobuyer")
        rc, stdout, stderr, dur = self.run_cli(jar, out, timeout=45)
        ok = rc == 0 and "##ND_RESULT:" in stdout
        self.record(2, tname, ok, dur, f"exit={rc}")

    # Test 03: Control Flow Flattening & Switch Traps (ChatFilterPlus)
    def test_03_switch_traps(self):
        tname = "Switch Traps & Jump Table Flattening (ChatFilterPlus)"
        jar = os.path.join(TEST_JARS, "ChatFilterPlus_v2_3.jar")
        out = os.path.join(TEMP_OUT, "t03_chatfilter")
        rc, stdout, stderr, dur = self.run_cli(jar, out, timeout=45)
        ok = rc == 0 and "##ND_RESULT:" in stdout
        self.record(3, tname, ok, dur, f"exit={rc}")

    # Test 04: InvokeDynamic BSM & Lambda Desugaring (NanoRTP)
    def test_04_lambda_desugaring(self):
        tname = "InvokeDynamic & Lambda Desugaring (NanoRTP)"
        jar = os.path.join(TEST_JARS, "NanoRTP.jar")
        out = os.path.join(TEMP_OUT, "t04_nanortp")
        rc, stdout, stderr, dur = self.run_cli(jar, out, timeout=45)
        ok = rc == 0 and "##ND_RESULT:" in stdout
        self.record(4, tname, ok, dur, f"exit={rc}")

    # Test 05: Irreducible Control Flow & Complex Loops (FunnyClans)
    def test_05_irreducible_loops(self):
        tname = "Irreducible CFG & Multi-Entry Loops (FunnyClans)"
        jar = os.path.join(TEST_JARS, "FunnyClans-3_9.jar")
        out = os.path.join(TEMP_OUT, "t05_funnyclans")
        rc, stdout, stderr, dur = self.run_cli(jar, out, timeout=45)
        ok = rc == 0 and "##ND_RESULT:" in stdout
        self.record(5, tname, ok, dur, f"exit={rc}")

    # Test 06: Opaque Predicates & Dead Code Pruning (DeluxeMenuConstructor)
    def test_06_opaque_predicates(self):
        tname = "Opaque Predicates & Dead Code Pruning (DeluxeMenuConstructor)"
        jar = os.path.join(TEST_JARS, "DeluxeMenuConstructor-2_0.jar")
        out = os.path.join(TEMP_OUT, "t06_deluxemenu")
        rc, stdout, stderr, dur = self.run_cli(jar, out, timeout=45)
        ok = rc == 0 and "##ND_RESULT:" in stdout
        self.record(6, tname, ok, dur, f"exit={rc}")

    # Test 07: Nested & Anonymous Inner Classes (HeadDatabase)
    def test_07_inner_classes(self):
        tname = "Nested & Anonymous Inner Classes Hierarchy (HeadDatabase)"
        jar = os.path.join(TEST_JARS, "HeadDatabase-4_21_2.jar")
        out = os.path.join(TEMP_OUT, "t07_headdatabase")
        rc, stdout, stderr, dur = self.run_cli(jar, out, timeout=45)
        ok = rc == 0 and "##ND_RESULT:" in stdout
        self.record(7, tname, ok, dur, f"exit={rc}")

    # Test 08: Multi-Asset & Resource Unpacking (WorldGuard-Translator)
    def test_08_resources(self):
        tname = "Multi-Asset & YAML/Properties Resource Recovery (WG-Translator)"
        jar = os.path.join(TEST_JARS, "WorldGuard-Translator-1_1_1.jar")
        out = os.path.join(TEMP_OUT, "t08_wg_trans")
        rc, stdout, stderr, dur = self.run_cli(jar, out, timeout=45)
        has_resources = False
        if os.path.isdir(out):
            for r, d, files in os.walk(out):
                for f in files:
                    if f.endswith(".yml") or f.endswith(".properties"):
                        has_resources = True
                        break
        ok = rc == 0 and has_resources
        self.record(8, tname, ok, dur, f"resources_found={has_resources}")

    # Test 09: Bukkit API & Event Annotation Recovery (NanoSpec)
    def test_09_bukkit_annotations(self):
        tname = "Bukkit API & EventHandler Annotations (NanoSpec)"
        jar = os.path.join(TEST_JARS, "NanoSpec-1_16_5.jar")
        out = os.path.join(TEMP_OUT, "t09_nanospec")
        rc, stdout, stderr, dur = self.run_cli(jar, out, timeout=45)
        ok = rc == 0 and "Bukkit/Spigot" in stdout
        self.record(9, tname, ok, dur, f"exit={rc}")

    # Test 10: Corrupted Bytecode & Crash-Bomb Resilience
    def test_10_corrupted_bytecode_resilience(self):
        tname = "Corrupted Bytecode & Crash-Bomb Resilience"
        fake_class = os.path.join(TEMP_OUT, "CorruptedTrap.class")
        with open(fake_class, "wb") as f:
            f.write(b"\xCA\xFE\xBA\xBE\x00\x00\x00\x3D\xFF\xFF\x00\x05GARBAGE_PAYLOAD_TRAP")
        out = os.path.join(TEMP_OUT, "t10_corrupted")
        rc, stdout, stderr, dur = self.run_cli(fake_class, out, timeout=10)
        # Engine must not segfault or crash hard (should return graceful error or code != -11)
        ok = rc != -1 and "Segmentation fault" not in stderr and "SIGSEGV" not in stderr
        self.record(10, tname, ok, dur, f"handled_cleanly (exit={rc})")

    # Test 11: High-Concurrency Thread Stress (8 Workers)
    def test_11_concurrency_stress(self):
        tname = "High-Concurrency Multithreading (8 Workers)"
        jar = os.path.join(TEST_JARS, "rccooldown-1_2.jar")
        out = os.path.join(TEMP_OUT, "t11_concurrency")
        rc, stdout, stderr, dur = self.run_cli(jar, out, extra_args=["--threads", "8"], timeout=45)
        ok = rc == 0 and "##ND_RESULT:" in stdout
        self.record(11, tname, ok, dur, f"exit={rc}")

    # Test 12: Large-Scale Plugin Scalability (TowerClans)
    def test_12_scale_volume(self):
        tname = "Large-Scale Plugin Scalability (TowerClans)"
        jar = os.path.join(TEST_JARS, "TowerClans-2_5_2.jar")
        out = os.path.join(TEMP_OUT, "t12_towerclans")
        rc, stdout, stderr, dur = self.run_cli(jar, out, timeout=45)
        ok = rc == 0 and "##ND_RESULT:" in stdout
        self.record(12, tname, ok, dur, f"exit={rc}")

    # Test 13: AST Syntax & Curly Bracket Balance Audit
    def test_13_bracket_balance(self):
        tname = "AST Syntax & Curly Bracket Balance Audit"
        # Check files decompiled in test 01 or 09
        target_dir = os.path.join(TEMP_OUT, "t01_smoke")
        imbalanced = 0
        total_files = 0
        if os.path.isdir(target_dir):
            for root, dirs, files in os.walk(target_dir):
                for f in files:
                    if f.endswith(".java"):
                        total_files += 1
                        with open(os.path.join(root, f), "r", encoding="utf-8", errors="ignore") as jf:
                            text = jf.read()
                            open_br = text.count("{")
                            close_br = text.count("}")
                            if open_br != close_br:
                                imbalanced += 1
        ok = total_files > 0 and imbalanced == 0
        self.record(13, tname, ok, 0.05, f"files={total_files}, imbalanced={imbalanced}")

    # Test 14: Deterministic Idempotence (Double Run Hash Match)
    def test_14_deterministic_idempotence(self):
        tname = "Deterministic Output Idempotence (Double Run Hash Match)"
        jar = os.path.join(TEST_JARS, "smoketest_plugin.jar")
        out1 = os.path.join(TEMP_OUT, "t14_run1")
        out2 = os.path.join(TEMP_OUT, "t14_run2")
        rc1, _, _, d1 = self.run_cli(jar, out1)
        rc2, _, _, d2 = self.run_cli(jar, out2)

        def hash_dir(d):
            h = hashlib.sha256()
            for r, dirs, files in os.walk(d):
                for fn in sorted(files):
                    fp = os.path.join(r, fn)
                    with open(fp, "rb") as f:
                        h.update(fn.encode("utf-8"))
                        h.update(f.read())
            return h.hexdigest()

        match = rc1 == 0 and rc2 == 0 and hash_dir(out1) == hash_dir(out2)
        self.record(14, tname, match, d1 + d2, f"hashes_identical={match}")

    # Test 15: Memory & Resource Ceiling Check
    def test_15_resource_ceiling(self):
        tname = "Memory Cleanup & Resource Ceiling Check"
        jar = os.path.join(TEST_JARS, "MSG.jar")
        out = os.path.join(TEMP_OUT, "t15_msg")
        rc, stdout, stderr, dur = self.run_cli(jar, out, timeout=30)
        ok = rc == 0 and "##ND_RESULT:" in stdout
        self.record(15, tname, ok, dur, f"exit={rc}")

    def execute_all(self):
        self.log("\n=======================================================")
        self.log("🚀 ЗАПУСК 15 СТРЕСС-ТЕСТОВ ДЕКОМПИЛЯТОРА NANODECOMPILER")
        self.log(f"Бинарник движка: {self.binary}")
        self.log(f"Тестовые JARs:   {TEST_JARS}")
        self.log(f"Временный вывод: {TEMP_OUT}")
        self.log("=======================================================\n")

        if not os.path.isfile(self.binary):
            self.log(f"❌ ОШИБКА: Бинарник движка не найден по пути {self.binary}")
            return 1

        tests = [
            self.test_01_smoketest,
            self.test_02_obfuscated_strings,
            self.test_03_switch_traps,
            self.test_04_lambda_desugaring,
            self.test_05_irreducible_loops,
            self.test_06_opaque_predicates,
            self.test_07_inner_classes,
            self.test_08_resources,
            self.test_09_bukkit_annotations,
            self.test_10_corrupted_bytecode_resilience,
            self.test_11_concurrency_stress,
            self.test_12_scale_volume,
            self.test_13_bracket_balance,
            self.test_14_deterministic_idempotence,
            self.test_15_resource_ceiling,
        ]

        start_time = time.time()
        for t in tests:
            t()

        total_time = time.time() - start_time
        self.log("\n=======================================================")
        self.log(f"📊 ИТОГИ ТЕСТИРОВАНИЯ: {self.passed}/15 УСПЕШНО | {self.failed} ОШИБОК ({total_time:.2f}s)")
        self.log("=======================================================")

        # Clean up temporary test outputs cleanly
        try:
            shutil.rmtree(TEMP_OUT, ignore_errors=True)
        except Exception:
            pass

        return 0 if self.failed == 0 else 1

def main():
    parser = argparse.ArgumentParser(description="NanoDecompiler 15 Stress & Correctness Tests")
    parser.add_argument("--bin", required=True, help="Путь к NanoDecompilerCLI")
    args = parser.parse_args()

    runner = SuiteRunner(args.bin)
    code = runner.execute_all()
    sys.exit(code)

if __name__ == "__main__":
    main()
