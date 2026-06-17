#!/usr/bin/env python3
#
# Copyright 2025, The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""
Script to dump oat boot image using METADATA.txt from prebuilt artifacts.
"""

import argparse
import contextlib
import logging
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path
from typing import List, Optional, Generator

# Configuration
_DEFAULT_FETCH_ARTIFACT = Path('/google/data/ro/projects/android/fetch_artifact')
_SUPPORTED_ARCHS = {'arm', 'arm64', 'riscv64', 'x86', 'x86_64'}
_BOOT_ZIP = 'boot.zip'
_TOOLS_ZIP = 'dexpreopt_tools.zip'
# Note: Tuple must have trailing comma to be iterable
_IGNORED_METADATA_EXTRA_ARGS = ('--assume-value',)

_EXAMPLE_USAGE = """
Example:
  python %(prog)s --bid 12345 --target aosp_cf_arm64_phone-trunk_staging-userdebug
"""

# Setup logging to write to stderr so stdout remains clean for piping data.
logging.basicConfig(level=logging.INFO, format='%(message)s', stream=sys.stderr)
logger = logging.getLogger(__name__)


def run_cmd(cmd: List[str], cwd: Optional[Path] = None) -> None:
    """Runs a shell command, checking for errors."""
    cmd_str = ' '.join(str(c) for c in cmd)
    logger.info(f"Executing: {cmd_str}")
    try:
        subprocess.run(
            cmd,
            check=True,
            cwd=cwd,
            text=True,
            stdout=sys.stdout,  # Pass stdout through to user
            stderr=sys.stderr   # Pass stderr through to user
        )
    except subprocess.CalledProcessError as e:
        logger.error(f"Failed execution: {e}")
        sys.exit(1)


def get_fetch_artifact_path() -> Path:
    """Locates the fetch_artifact binary."""
    path = shutil.which("fetch_artifact")
    if path:
        return Path(path)

    if _DEFAULT_FETCH_ARTIFACT.exists():
        return _DEFAULT_FETCH_ARTIFACT

    logger.error(
        "Error: cannot find fetch_artifact in PATH.\n"
        "Try installing it via:\n"
        "  sudo glinux-add-repo android\n"
        "  sudo apt update\n"
        "  sudo apt install android-fetch-artifact"
    )
    sys.exit(1)


def fetch_artifact(bid: str, target: str, artifact: str, output_dir: Path) -> None:
    """Fetches a prebuilt artifact to the specified directory."""
    fetcher = get_fetch_artifact_path()
    cmd = [
        str(fetcher),
        '--bid', bid,
        '--target', target,
        artifact,
        str(output_dir)
    ]
    run_cmd(cmd)


def unzip_artifact(zip_path: Path, output_dir: Path) -> None:
    """Unzips an artifact to the output directory."""
    logger.info(f"Unzipping {zip_path} to {output_dir}")
    with zipfile.ZipFile(zip_path, 'r') as zip_ref:
        zip_ref.extractall(output_dir)


def parse_metadata(metadata_file: Path) -> dict[str, str]:
    """Parses METADATA.txt into a dictionary."""
    args = {}
    with metadata_file.open('r') as f:
        for line in f:
            line = line.strip()
            if not line or '=' not in line:
                continue
            key, value = line.split('=', 1)
            args[key.strip()] = value.strip()
    return args


def detect_arch(boot_dir: Path) -> str:
    """Detects architecture by inspecting boot.art location subdirectories."""
    found_archs = set()
    for match in boot_dir.rglob('boot.art'):
        arch = match.parent.name
        if arch in _SUPPORTED_ARCHS:
            found_archs.add(arch)

    if 'arm64' in found_archs:
        return 'arm64'
    if 'x86_64' in found_archs:
        return 'x86_64'

    if found_archs:
        return found_archs.pop()

    # Default fallback
    return 'arm64'


@contextlib.contextmanager
def get_work_dir(user_dir: Optional[str]) -> Generator[Path, None, None]:
    """Context manager for the workspace.

    If user_dir is provided, uses it (and does not delete it).
    If not, creates a temporary directory and deletes it on exit.
    """
    if user_dir:
        path = Path(user_dir)
        path.mkdir(parents=True, exist_ok=True)
        yield path
    else:
        with tempfile.TemporaryDirectory(prefix='dump_oat_boot_') as temp_dir:
            yield Path(temp_dir)


def main():
    parser = argparse.ArgumentParser(
        description='Runs oatdump on the boot image from prebuilt artifacts.',
        epilog=_EXAMPLE_USAGE,
        formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument('--bid', required=True, help='Build ID (e.g., 12345)')
    parser.add_argument('--target', required=True, help='Build Target')
    parser.add_argument('--output-dir', help='Directory to use for artifacts (default: temp dir)')
    parser.add_argument('--oatdump-path', type=Path, help='Path to oatdump binary (default: fetch from tools zip)')
    parser.add_argument('--instruction-set', choices=_SUPPORTED_ARCHS, help='Override instruction set')
    parser.add_argument('--primary-only', action='store_true', help='Only dump the primary boot image artifact')
    parser.add_argument('extra_args', nargs=argparse.REMAINDER, help='Extra arguments to pass to oatdump')

    args = parser.parse_args()

    with get_work_dir(args.output_dir) as work_dir:
        logger.info(f"Working directory: {work_dir}")

        # 1. Fetch and extract boot image artifacts if necessary
        boot_zip = work_dir / _BOOT_ZIP
        if not boot_zip.exists():
            fetch_artifact(args.bid, args.target, _BOOT_ZIP, work_dir)

        boot_dir = work_dir / 'boot'
        if not boot_dir.exists():
            unzip_artifact(boot_zip, boot_dir)

        # 2. Fetch and extract the oatdump binary if necessary.
        oatdump_bin = args.oatdump_path
        if not oatdump_bin:
            tools_zip = work_dir / _TOOLS_ZIP
            if not tools_zip.exists():
                fetch_artifact(args.bid, args.target, _TOOLS_ZIP, work_dir)

            tools_dir = work_dir / 'tools'
            if not tools_dir.exists():
                unzip_artifact(tools_zip, tools_dir)

            try:
                oatdump_bin = next(tools_dir.rglob('oatdump'))
                oatdump_bin.chmod(0o755)
            except StopIteration:
                logger.error("Could not find oatdump in dexpreopt_tools.zip")
                sys.exit(1)

        # 3. Parse prebuilt metadata for oatdump arguments.
        metadata_file = boot_dir / 'METADATA.txt'
        if not metadata_file.exists():
            logger.error("METADATA.txt not found in boot.zip. Cannot proceed.")
            sys.exit(1)

        metadata = parse_metadata(metadata_file)

        # 4. Construct the oatdump command.
        arch = args.instruction_set or detect_arch(boot_dir)

        boot_image = metadata.get('boot-image', '')
        if not boot_image:
             logger.error(f"`boot-image` definition not found in METADATA.txt: \n{metadata}")
             sys.exit(1)

        if args.primary_only:
            image_arg = boot_image.split(':')[0]
        else:
            image_arg = boot_image

        cmd = [
            str(oatdump_bin.resolve()),
            f'--image={image_arg}',
            f'--instruction-set={arch}'
        ]

        if 'bootclasspath' in metadata:
            cmd.extend(['--runtime-arg', f'-Xbootclasspath:{metadata["bootclasspath"]}'])

        if 'bootclasspath-locations' in metadata:
            cmd.extend(['--runtime-arg', f'-Xbootclasspath-locations:{metadata["bootclasspath-locations"]}'])

        if 'extra-args' in metadata:
            raw_args = metadata['extra-args'].split()
            # Filter out compiler-specific args
            filtered_args = [
                arg for arg in raw_args
                if not arg.startswith(_IGNORED_METADATA_EXTRA_ARGS)
            ]
            cmd.extend(filtered_args)

        if args.extra_args:
            cmd.extend(args.extra_args)

        # 5. Execute the oatdump command.
        # We run in boot_dir so relative paths in the metadata work correctly
        run_cmd(cmd, cwd=boot_dir)


if __name__ == '__main__':
    main()