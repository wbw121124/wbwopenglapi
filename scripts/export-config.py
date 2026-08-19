#!/usr/bin/env python3
# export-config.py - 将根目录 config.yml 的扁平键值导出为 KEY=VALUE 行
# （供 GitHub Actions 步骤追加到 GITHUB_ENV；嵌套键以 "_" 连接并大写）
# 用法: python scripts/export-config.py  [-c <config.yml 路径>]
# 依赖 PyYAML：缺失时自动 pip 安装。
import subprocess
import sys

try:
    import yaml
except ImportError:
    subprocess.check_call([sys.executable, "-m", "pip", "install", "--quiet", "pyyaml"])
    import yaml

path = "config.yml"
if "-c" in sys.argv:
    path = sys.argv[sys.argv.index("-c") + 1]

with open(path, encoding="utf-8") as fh:
    data = yaml.safe_load(fh) or {}


def walk(node, prefix=""):
    for key, val in node.items():
        name = prefix + key if not prefix else prefix + "_" + key
        if isinstance(val, dict):
            walk(val, name)
        elif isinstance(val, (str, int, float, bool)):
            print(f"{name.upper()}={val}")


walk(data)