#!/usr/bin/env python3

import argparse
import json
import os
import subprocess
import tempfile
from pathlib import Path


def run(command: list[str], *, cwd: Path, timeout: int) -> subprocess.CompletedProcess[str]:
    completed = subprocess.run(
        command,
        cwd=cwd,
        capture_output=True,
        text=True,
        timeout=timeout,
        check=False,
    )
    if completed.returncode != 0:
        raise RuntimeError(
            f"command exited with {completed.returncode}: {' '.join(command)}\n"
            f"stdout:\n{completed.stdout}\n"
            f"stderr:\n{completed.stderr}"
        )
    return completed


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Verify the bundled pnpm command under QuickJS WASIX."
    )
    parser.add_argument(
        "--wasmer-bin",
        default=os.environ.get("WASMER_BIN", "wasmer"),
        help="Path to the Wasmer CLI binary.",
    )
    parser.add_argument(
        "--package-dir",
        default=str(Path(__file__).resolve().parents[1] / "quickjs-wasm"),
        help="QuickJS WASIX package directory.",
    )
    parser.add_argument("--timeout", type=int, default=60)
    args = parser.parse_args()

    package_dir = Path(args.package_dir).resolve()
    if not (package_dir / "wasmer.toml").is_file():
        raise RuntimeError(f"missing wasmer.toml in {package_dir}")

    with tempfile.TemporaryDirectory(prefix="edgejs-pnpm-smoke.") as temp_dir:
        project_dir = Path(temp_dir)
        (project_dir / "package.json").write_text(
            json.dumps({"name": "edge-pnpm-smoke", "private": True}, indent=2) + "\n",
            encoding="utf-8",
        )

        base = [args.wasmer_bin, "run", str(package_dir)]
        version = run(
            base + ["--command=pnpm", "--volume=.", "--", "--version"],
            cwd=project_dir,
            timeout=args.timeout,
        ).stdout.strip()
        if version != "10.34.5":
            raise RuntimeError(f"unexpected pnpm version: {version!r}")

        store_path = run(
            base + ["--command=pnpm", "--volume=.", "--", "store", "path"],
            cwd=project_dir,
            timeout=args.timeout,
        ).stdout.strip()
        if store_path != "/tmp/.pnpm-store/v10":
            raise RuntimeError(f"unexpected pnpm store path: {store_path!r}")

        install = run(
            base
            + [
                "--command=pnpm",
                "--net",
                "--volume=.",
                "--",
                "install",
                "react@19.2.8",
            ],
            cwd=project_dir,
            timeout=args.timeout,
        )
        install_output = install.stdout + install.stderr
        if "Update available!" in install_output:
            raise RuntimeError(
                "pnpm's update notification was not disabled:\n" + install_output
            )

        manifest = json.loads((project_dir / "package.json").read_text(encoding="utf-8"))
        if manifest.get("dependencies", {}).get("react") != "19.2.8":
            raise RuntimeError(f"React was not persisted in package.json: {manifest!r}")
        if not (project_dir / "pnpm-lock.yaml").is_file():
            raise RuntimeError("pnpm-lock.yaml was not created")
        if not (project_dir / "node_modules" / "react" / "package.json").is_file():
            raise RuntimeError("hoisted node_modules/react package was not materialized")

        edge = run(
            base
            + [
                "--command=edge",
                "--volume=.",
                "--",
                "-e",
                "console.log(require('react').version)",
            ],
            cwd=project_dir,
            timeout=args.timeout,
        )
        if edge.stdout.strip() != "19.2.8":
            raise RuntimeError(f"Edge could not resolve installed React: {edge.stdout!r}")

    print("Bundled pnpm WASIX smoke test passed (pnpm 10.34.5, React 19.2.8).")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (RuntimeError, subprocess.TimeoutExpired) as error:
        print(error, file=os.sys.stderr)
        raise SystemExit(1)
