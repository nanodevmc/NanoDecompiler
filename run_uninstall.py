import subprocess
import os
import time

uninstaller = os.path.expandvars(r"%LOCALAPPDATA%\Programs\NanoDecompiler\Uninstall NanoDecompiler.exe")
if os.path.exists(uninstaller):
    print("Running uninstaller:", uninstaller)
    subprocess.run([uninstaller, "/S"], check=True)
    time.sleep(2)
    print("Uninstalled successfully!")
else:
    print("Uninstaller not found, checking directory...")

prog_dir = os.path.expandvars(r"%LOCALAPPDATA%\Programs\NanoDecompiler")
if os.path.exists(prog_dir):
    print("Remaining files in prog_dir:", os.listdir(prog_dir))
    import shutil
    try:
        shutil.rmtree(prog_dir)
        print("Cleaned up remaining directory:", prog_dir)
    except Exception as e:
        print("Cleanup error:", e)
else:
    print("Program directory is completely removed.")
