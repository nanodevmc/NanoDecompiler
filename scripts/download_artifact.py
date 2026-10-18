import io
import os
import urllib.parse
import urllib.request
import zipfile

from _gh_auth import get_token


class NoAuthRedirect(urllib.request.HTTPRedirectHandler):
    """Не отправляем токен на сторонние хосты при редиректе (артефакты отдаются с
    подписанного адреса на другом домене). Проверка по ИМЕНИ ХОСТА, а не по подстроке
    "github.com" в URL: раньше адрес вида https://evil.example/github.com пропускал токен."""

    def redirect_request(self, req, fp, code, msg, headers, newurl):
        new_req = super().redirect_request(req, fp, code, msg, headers, newurl)
        host = (urllib.parse.urlparse(newurl).hostname or "").lower()
        if new_req is not None and host not in ("github.com", "api.github.com"):
            new_req.headers.pop("Authorization", None)
            new_req.headers.pop("authorization", None)
        return new_req


# Токен берётся из GITHUB_TOKEN / GH_TOKEN / `gh auth token`, а не из git remote URL
# (там он лежал открытым текстом в .git/config) - см. scripts/_gh_auth.py.
token = get_token()
opener = urllib.request.build_opener(NoAuthRedirect)

url = "https://api.github.com/repos/NanoDev1488/NanoDecompiler/actions/artifacts/12009945962/zip"
headers = {
    "User-Agent": "Mozilla/5.0",
    "Accept": "application/vnd.github.v3+json",
}
if token:
    headers["Authorization"] = f"Bearer {token}"
req = urllib.request.Request(url, headers=headers)

try:
    with opener.open(req) as resp:
        data = resp.read()
        print(f"Downloaded zip: {len(data)} bytes")
        with zipfile.ZipFile(io.BytesIO(data)) as z:
            os.makedirs("artifact_itemsadder_new", exist_ok=True)
            z.extractall("artifact_itemsadder_new")
            print("Extracted files:", z.namelist())
except Exception as e:
    print("Download error:", e)
