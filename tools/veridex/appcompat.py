#
# Copyright (C) 2024 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import os
import subprocess
import sys
import pkgutil
import tempfile

def get_base_dir():
    # Soong binary sets the script path to argv[0]
    script_path = os.path.abspath(sys.argv[0])
    if os.path.isfile(script_path):
        return os.path.dirname(script_path)
    return os.path.dirname(os.path.abspath(__file__))

def get_resource_path(resource_name, mode=0o644):
    """Retrieves a resource path.

    Checks the script's directory first. If not found, retrieves it from the
    package resources and writes it to a temporary file.

    Args:
        resource_name: The name of the resource.
        mode: File permissions mode (default is 0o644).
    """
    script_dir = get_base_dir()
    local_path = os.path.join(script_dir, resource_name)
    if os.path.exists(local_path):
        print("Use local resource:", resource_name)
        return local_path

    data = pkgutil.get_data(__name__, resource_name)
    if data is None:
        raise FileNotFoundError(f"Resource not found in package: {resource_name}")

    if not data:
        msg = f"Resource '{resource_name}' is empty."
        if resource_name == "hiddenapi-flags.csv" or resource_name == "hiddenapi-flagged-apis.csv":
            msg += (" This usually happens in 'eng' builds where hidden API checks "
                    "are disabled by default. Please rebuild with "
                    "'ENABLE_HIDDENAPI_FLAGS=true m appcompat'.")
        raise RuntimeError(msg)

    with tempfile.NamedTemporaryFile(delete=False) as temp_file:
        temp_file.write(data)
        resource_path = temp_file.name
    os.chmod(resource_path, mode)  # Set permissions
    return resource_path

def main():
    print("NOTE: appcompat is still under development. It can report")
    print("API uses that do not execute at runtime, and reflection uses")
    print("that do not exist. It can also miss on reflection uses.")

    veridex_path = get_resource_path("veridex", 0o755)
    hiddenapi_flags_path = get_resource_path("hiddenapi-flags.csv")
    flagged_apis_path = get_resource_path("hiddenapi-flagged-apis.csv")
    system_stubs_path = get_resource_path("system-stubs.zip")
    http_legacy_stubs_path = get_resource_path("org.apache.http.legacy-stubs.zip")

    args = [
        veridex_path,
        f"--core-stubs={system_stubs_path}:{http_legacy_stubs_path}",
        f"--api-flags={hiddenapi_flags_path}",
        f"--flagged-apis={flagged_apis_path}",
        "--exclude-api-lists=sdk,invalid",
    ] + sys.argv[1:]

    subprocess.run(args)

if __name__ == "__main__":
    main()
