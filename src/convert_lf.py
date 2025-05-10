#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
遍历 src/test 目录，将每个文件通过 dos2unix 转换为 LF 格式。
"""

import os
import subprocess
import sys

def main():
    # 确定目标目录：脚本所在目录的 src/test
    base_dir = os.path.dirname(os.path.abspath(__file__))
    target_dir = os.path.join(base_dir, 'tests')

    if not os.path.isdir(target_dir):
        print(f"错误：未找到目录 {target_dir}", file=sys.stderr)
        sys.exit(1)

    converted = 0
    total = 0

    # 递归遍历所有文件
    for root, _, files in os.walk(target_dir):
        for name in files:
            total += 1
            path = os.path.join(root, name)
            print(f"转换中：{path}")
            # 调用 dos2unix 命令
            # 「shell=False」更安全，推荐使用列表形式
            result = subprocess.run(
                ['dos2unix', path],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE
            )
            if result.returncode == 0:
                converted += 1
            else:
                print(f"[警告] 转换失败：{path}\n  错误信息：{result.stderr.decode().strip()}", file=sys.stderr)

    print(f"\n完成！共处理 {total} 个文件，成功转换 {converted} 个。")

if __name__ == '__main__':
    main()
