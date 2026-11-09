"""Format C++ added by this fork, leaving upstream files and the copied JPEG codec alone."""
from pathlib import Path
import argparse
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BASE = "cf3d1ce8ecfd7d4e4c63c52c9e25358dcdc52637"
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--check", action="store_true")
parser.add_argument("--formatter", default="clang-format", help="clang-format 18.1.8 executable")
args = parser.parse_args()
version = subprocess.check_output([args.formatter, "--version"], text=True)
if "version 18.1.8" not in version:
    raise SystemExit("Use clang-format 18.1.8 for reproducible formatting")
paths = subprocess.check_output(["git", "diff", "--name-only", "--diff-filter=A", BASE], cwd=ROOT, text=True).splitlines()
paths = [p for p in paths if p.endswith((".cpp", ".h")) and "cine_bench_jpeg" not in p]
if paths:
    flags = ["--dry-run", "--Werror"] if args.check else ["-i"]
    style = "{BasedOnStyle: LLVM, IndentWidth: 2, ColumnLimit: 100, SortIncludes: Never}"
    subprocess.run([args.formatter, "--style=" + style] + flags + paths, cwd=ROOT, check=True)
