"""Общий помощник авторизации в GitHub API для служебных скриптов.

БЕЗОПАСНОСТЬ: раньше каждый скрипт доставал личный токен доступа регуляркой
из `git config remote.origin.url` (формат https://TOKEN@github.com/...), то
есть токен хранился открытым текстом в .git/config, попадал в вывод
`git remote -v`, в логи и бэкапы. Теперь токен берётся ТОЛЬКО из переменных
окружения GITHUB_TOKEN / GH_TOKEN либо из `gh auth token` (GitHub CLI), а
токен внутри URL remote отвергается с подсказкой, как это исправить.
"""

import os
import re
import shutil
import subprocess
import sys


def _remote_url():
    try:
        return subprocess.check_output(
            ["git", "config", "--get", "remote.origin.url"],
            text=True,
            stderr=subprocess.DEVNULL,
        ).strip()
    except Exception:
        return ""


def get_token():
    for name in ("GITHUB_TOKEN", "GH_TOKEN"):
        value = os.environ.get(name, "").strip()
        if value:
            return value

    gh = shutil.which("gh")
    if gh:
        try:
            value = subprocess.check_output([gh, "auth", "token"], text=True, stderr=subprocess.DEVNULL).strip()
            if value:
                return value
        except Exception:
            pass

    if re.search(r"https://[^/@\s]+@github\.com", _remote_url()):
        print(
            "ВНИМАНИЕ: токен найден прямо в remote.origin.url - так делать нельзя "
            "(он лежит открытым текстом в .git/config). Выполните\n"
            "  git remote set-url origin https://github.com/NanoDev1488/NanoDecompiler.git\n"
            "отзовите (ревокните) этот токен на https://github.com/settings/tokens, создайте новый и "
            "передайте его через переменную окружения GITHUB_TOKEN или `gh auth login`.",
            file=sys.stderr,
        )
    return None


def github_headers(extra=None):
    headers = {"User-Agent": "NanoDecompiler-scripts"}
    if extra:
        headers.update(extra)
    token = get_token()
    if token:
        headers["Authorization"] = f"Bearer {token}"
    return headers
