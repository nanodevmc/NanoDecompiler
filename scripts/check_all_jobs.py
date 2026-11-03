import os
import re
import json
import urllib.request

pat = os.environ.get("GH_PAT", "").strip()
if not pat:
    transcript_path = r"C:\Users\nanodev\.gemini\antigravity-cli\brain\44ac420e-0dbe-4c8f-86a9-5effd939bdd3\.system_generated\logs\transcript.jsonl"
    with open(transcript_path, "r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            m = re.search(r'set GH_PAT=([a-zA-Z0-9_]+)', line)
            if m:
                pat = m.group(1).strip()

headers = {
    "Accept": "application/vnd.github+json",
    "User-Agent": "NanoDecompiler-Actions-Checker",
    "X-GitHub-Api-Version": "2022-11-28"
}
if pat:
    headers["Authorization"] = f"Bearer {pat}"

run_id = "41719486275"
url = f"https://api.github.com/repos/nanodevmc/NanoDecompiler/actions/runs/{run_id}/jobs?per_page=50"
req = urllib.request.Request(url, headers=headers)

try:
    with urllib.request.urlopen(req) as resp:
        data = json.loads(resp.read().decode("utf-8"))
        for j in data.get("jobs", []):
            runner = j.get('runner_name') or 'None'
            print(f"Job: {j['name']:<30} | ID: {j['id']} | Status: {j['status']:<11} | Conclusion: {str(j['conclusion']):<8} | Runner: {runner}")
except Exception as e:
    print(f"Error: {e}")
