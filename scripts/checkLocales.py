import json
import re
import sys
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")
root = Path(__file__).resolve().parent.parent
ui = root / "ui"
locales = {path.stem: json.loads(path.read_text(encoding="utf-8")) for path in sorted((ui / "locales").glob("*.json"))}
problems = []


def placeholders(message):
    texts = message.values() if isinstance(message, dict) else [message]
    return {name for text in texts for name in re.findall(r"\{(\w+)\}", text)}


base_name, base = next(iter(locales.items()))
for name, messages in locales.items():
    for key in base.keys() - messages.keys():
        problems.append(f"{name} 少了 {key}")
    for key in messages.keys() - base.keys():
        problems.append(f"{name} 多了 {key}（{base_name} 沒有）")
    for key in base.keys() & messages.keys():
        if placeholders(base[key]) != placeholders(messages[key]):
            problems.append(f"{name} 的 {key} 參數跟 {base_name} 不一樣")
        if isinstance(messages[key], dict) and "other" not in messages[key]:
            problems.append(f"{name} 的 {key} 少了 other")

sources = [ui / "index.html", *sorted((ui / "js").rglob("*.js"))]
code = "\n".join(path.read_text(encoding="utf-8") for path in sources)
exact = set(re.findall(r"""\bt\(\s*'([\w.]+)'""", code))
exact |= set(re.findall(r"""data-i18n(?:-placeholder|-label)?="([\w.]+)\"""", code))
exact |= set(re.findall(r"""[?:]\s*'([a-zA-Z]+\.[\w.]+)'""", code))
exact |= set(re.findall(r"""key: '([\w.]+)'""", code))
exact |= set(re.findall(r"""(?:noticeOf|button)\([^)]*?'((?:stage|account)\.[\w.]+)'""", code))
prefixes = set(re.findall(r"""\bt\(`([\w.]*)\$\{""", code))

for key in sorted(exact - base.keys()):
    if re.fullmatch(r"[a-z][\w]*(\.[\w]+)+", key) and key.split(".")[0] in {k.split(".")[0] for k in base}:
        problems.append(f"程式用到字典裡沒有的 {key}")
for key in sorted(base.keys() - exact):
    if not any(prefix and key.startswith(prefix) for prefix in prefixes) and not re.match(r"(eq|reverb|direction|align)\.", key):
        problems.append(f"字典裡的 {key} 沒有程式在用")

for problem in problems:
    print(problem)
print(f"{len(base)} 句、{len(locales)} 種語言，{len(problems)} 個問題")
sys.exit(1 if problems else 0)
