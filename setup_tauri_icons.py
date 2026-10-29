import os
import shutil

icons_dir = r"C:\Users\nanodev\.gemini\antigravity\scratch\NanoDecompiler\tauri-v2\src-tauri\icons"
os.makedirs(icons_dir, exist_ok=True)

# Copy ico
ico_src = r"C:\Users\nanodev\.gemini\antigravity\scratch\NanoDecompiler\resources\icons\icon-b-terminal.ico"
shutil.copyfile(ico_src, os.path.join(icons_dir, "icon.ico"))

# Copy png
png_src = r"C:\Users\nanodev\.gemini\antigravity\scratch\NanoDecompiler\resources\icons\icon-b-terminal.png"
shutil.copyfile(png_src, os.path.join(icons_dir, "icon.png"))
shutil.copyfile(png_src, os.path.join(icons_dir, "128x128.png"))
shutil.copyfile(png_src, os.path.join(icons_dir, "32x32.png"))

print("Icons copied successfully into", icons_dir)
