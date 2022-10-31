#
# Copyright (C) 2021 The Android Open Source Project
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

"""This is the default build script for run-tests.

It can be overwrite by specific run-tests if needed.
It is used from soong build and not intended to be called directly.
"""

import argparse
import functools
import glob
import os
from os import path
import shlex
import shutil
import subprocess
import tempfile
import zipfile
from shutil import rmtree
from os import remove
from re import match
from os.path import join

USE_RBE_FOR_JAVAC = 100    # Percentage of tests that can use RBE (between 0 and 100)
USE_RBE_FOR_D8 = 100       # Percentage of tests that can use RBE (between 0 and 100)

class BuildTestContext:
  def __init__(self, args, build_top, sbox, test_name, test_dir):
    self.test_dir = test_dir
    self.mode = args.mode
    self.jvm = (self.mode == "jvm")
    self.host = (self.mode == "host")
    self.target = (self.mode == "target")
    assert self.jvm or self.host or self.target

    java_home = os.environ.get("JAVA_HOME")
    tools_dir = os.path.abspath(join(os.path.dirname(__file__), "../../../out/bin"))
    self.android_build_top = build_top
    self.art_test_run_test_bootclasspath = join(build_top, args.bootclasspath)
    self.d8 = join(tools_dir, "d8")
    self.d8_flags = []
    self.hiddenapi = join(tools_dir, "hiddenapi")
    self.jasmin = join(tools_dir, "jasmin")
    self.java = join(java_home, "bin/java")
    self.javac = join(java_home, "bin/javac")
    self.javac_args = "-g -Xlint:-options -source 1.8 -target 1.8"
    self.need_dex = (self.host or self.target)
    self.sbox_path = sbox
    self.smali = join(tools_dir, "smali")
    self.smali_flags = []
    self.soong_zip = join(build_top, "prebuilts/build-tools/linux-x86/bin/soong_zip")
    self.test_name = test_name
    self.zipalign = join(build_top, "prebuilts/build-tools/linux-x86/bin/zipalign")

    # Minimal environment needed for bash commands that we execute.
    self.bash_env = {
      "ANDROID_BUILD_TOP": self.android_build_top,
      "D8": self.d8,
      "JAVA": self.java,
      "JAVAC": self.javac,
      "JAVAC_ARGS": self.javac_args,
      "JAVA_HOME": java_home,
      "PATH": os.environ["PATH"],
      "PYTHONDONTWRITEBYTECODE": "1",
      "SMALI": self.smali,
      "SOONG_ZIP": self.soong_zip,
      "TEST_NAME": self.test_name,
    }

  def bash(self, cmd):
    return subprocess.run(cmd, shell=True, env=self.bash_env, check=True)

  def default_build(self, **kwargs):
    globals()['default_build'](self, **kwargs)

def rm(*patterns):
  for pattern in patterns:
    for path in glob.glob(pattern):
      if os.path.isdir(path):
        shutil.rmtree(path)
      else:
        os.remove(path)

def default_build(
    ctx,
    use_desugar=True,
    use_hiddenapi=True,
    need_dex=None,
    experimental="no-experiment",
    zip_compression_method="deflate",
    zip_align_bytes=None,
    api_level=None,
    javac_args=[],
    d8_flags=[],
    smali_args=[],
    has_smali=None,
    has_jasmin=None,
  ):

  ANDROID_BUILD_TOP = ctx.android_build_top
  SBOX_PATH = ctx.sbox_path
  CWD = os.getcwd()
  TEST_NAME = ctx.test_name
  ART_TEST_RUN_TEST_BOOTCLASSPATH = path.relpath(ctx.art_test_run_test_bootclasspath, CWD)
  NEED_DEX = ctx.need_dex if need_dex is None else need_dex

  RBE_exec_root = os.environ.get("RBE_exec_root")
  RBE_rewrapper = path.join(ANDROID_BUILD_TOP, "prebuilts/remoteexecution-client/live/rewrapper")

  # Set default values for directories.
  HAS_SMALI = path.exists("smali") if has_smali is None else has_smali
  HAS_JASMIN = path.exists("jasmin") if has_jasmin is None else has_jasmin
  HAS_SRC = path.exists("src")
  HAS_SRC_ART = path.exists("src-art")
  HAS_SRC2 = path.exists("src2")
  HAS_SRC_MULTIDEX = path.exists("src-multidex")
  HAS_SMALI_MULTIDEX = path.exists("smali-multidex")
  HAS_JASMIN_MULTIDEX = path.exists("jasmin-multidex")
  HAS_SMALI_EX = path.exists("smali-ex")
  HAS_SRC_EX = path.exists("src-ex")
  HAS_SRC_EX2 = path.exists("src-ex2")
  HAS_SRC_AOTEX = path.exists("src-aotex")
  HAS_SRC_BCPEX = path.exists("src-bcpex")
  HAS_HIDDENAPI_SPEC = path.exists("hiddenapi-flags.csv")

  JAVAC_ARGS = shlex.split(ctx.javac_args) + javac_args
  SMALI_ARGS = ctx.smali_flags + smali_args
  D8_FLAGS = ctx.d8_flags + d8_flags

  BUILD_MODE = ctx.mode

  # Setup experimental API level mappings in a bash associative array.
  EXPERIMENTAL_API_LEVEL = {}
  EXPERIMENTAL_API_LEVEL["no-experiment"] = "26"
  EXPERIMENTAL_API_LEVEL["default-methods"] = "24"
  EXPERIMENTAL_API_LEVEL["parameter-annotations"] = "25"
  EXPERIMENTAL_API_LEVEL["agents"] = "26"
  EXPERIMENTAL_API_LEVEL["method-handles"] = "26"
  EXPERIMENTAL_API_LEVEL["var-handles"] = "28"

  if BUILD_MODE == "jvm":
    # No desugaring on jvm because it supports the latest functionality.
    use_desugar = False
    # Do not attempt to build src-art directories on jvm,
    # since it would fail without libcore.
    HAS_SRC_ART = False

  # Set API level for smali and d8.
  if not api_level:
    api_level = EXPERIMENTAL_API_LEVEL[experimental]

  # Add API level arguments to smali and dx
  SMALI_ARGS.extend(["--api", str(api_level)])
  D8_FLAGS.extend(["--min-api", str(api_level)])

  def run(executable, args):
    cmd = shlex.split(executable) + args
    if executable.endswith(".sh"):
      cmd = ["/bin/bash"] + cmd
    env = ctx.bash_env
    env.update({k: v for k, v in os.environ.items() if k.startswith("RBE_")})
    p = subprocess.run(cmd,
                       encoding=os.sys.stdout.encoding,
                       env=ctx.bash_env,
                       stderr=subprocess.STDOUT,
                       stdout=subprocess.PIPE)
    if p.returncode != 0:
      raise Exception("Command failed with exit code {}\n$ {}\n{}".format(
                      p.returncode, " ".join(cmd), p.stdout))
    return p


  # Helper functions to execute tools.
  soong_zip = functools.partial(run, ctx.soong_zip)
  zipalign = functools.partial(run, ctx.zipalign)
  javac = functools.partial(run, ctx.javac)
  jasmin = functools.partial(run, ctx.jasmin)
  smali = functools.partial(run, ctx.smali)
  d8 = functools.partial(run, ctx.d8)
  hiddenapi = functools.partial(run, ctx.hiddenapi)

  if "RBE_server_address" in os.environ:
    version = match(r"Version: (\d*)\.(\d*)\.(\d*)", run(RBE_rewrapper, ["--version"]).stdout)
    assert version, "Could not parse RBE version"
    assert tuple(map(int, version.groups())) >= (0, 76, 0), "Please update " + RBE_rewrapper

    def rbe_wrap(args, inputs=set()):
      with tempfile.NamedTemporaryFile(mode="w+t", dir=RBE_exec_root) as input_list:
        for arg in args:
          inputs.update(filter(path.exists, arg.split(":")))
        input_list.writelines([path.relpath(i, RBE_exec_root)+"\n" for i in inputs])
        input_list.flush()
        return run(RBE_rewrapper, [
          "--platform=" + os.environ["RBE_platform"],
          "--input_list_paths=" + input_list.name,
        ] + args)

    if USE_RBE_FOR_JAVAC > (hash(TEST_NAME) % 100):  # Use for given percentage of tests.
      def javac(args):
        output = path.relpath(path.join(CWD, args[args.index("-d") + 1]), RBE_exec_root)
        return rbe_wrap([
          "--output_directories", output,
          os.path.relpath(ctx.javac, CWD),
        ] + args)

    if USE_RBE_FOR_D8 > (hash(TEST_NAME) % 100):  # Use for given percentage of tests.
      def d8(args):
        inputs = set([path.join(SBOX_PATH, "tools/out/framework/d8.jar")])
        output = path.relpath(path.join(CWD, args[args.index("--output") + 1]), RBE_exec_root)
        return rbe_wrap([
          "--output_files" if output.endswith(".jar") else "--output_directories", output,
          "--toolchain_inputs=prebuilts/jdk/jdk11/linux-x86/bin/java",
          os.path.relpath(ctx.d8, CWD)] + args, inputs)

  # If wrapper script exists, use it instead of the default javac.
  if os.path.exists("javac_wrapper.sh"):
    javac = functools.partial(run, "javac_wrapper.sh")

  def find(root, name):
    return sorted(glob.glob(path.join(root, "**", name), recursive=True))


  def zip(zip_target, *files):
    zip_args = ["-o", zip_target]
    if zip_compression_method == "store":
      zip_args.extend(["-L", "0"])
    for f in files:
      zip_args.extend(["-f", f])
    soong_zip(zip_args)

    if zip_align_bytes:
      # zipalign does not operate in-place, so write results to a temp file.
      with tempfile.TemporaryDirectory(dir=".") as tmp_dir:
        tmp_file = path.join(tmp_dir, "aligned.zip")
        zipalign(["-f", str(zip_align_bytes), zip_target, tmp_file])
        # replace original zip target with our temp file.
        os.rename(tmp_file, zip_target)


  def make_jasmin(out_directory, jasmin_sources):
    os.makedirs(out_directory, exist_ok=True)
    jasmin(["-d", out_directory] + sorted(jasmin_sources))


  # Like regular javac but may include libcore on the bootclasspath.
  def javac_with_bootclasspath(args):
    flags = JAVAC_ARGS + ["-encoding", "utf8"]
    if BUILD_MODE != "jvm":
      flags.extend(["-bootclasspath", ART_TEST_RUN_TEST_BOOTCLASSPATH])
    javac(flags + args)


  # Make a "dex" file given a directory of classes. This will be
  # packaged in a jar file.
  def make_dex(name):
    d8_inputs = find(name, "*.class")
    d8_output = name + ".jar"
    dex_output = name + ".dex"
    if use_desugar:
      flags = ["--lib", ART_TEST_RUN_TEST_BOOTCLASSPATH]
    else:
      flags = ["--no-desugaring"]
    assert d8_inputs
    d8(D8_FLAGS + flags + ["--output", d8_output] + d8_inputs)

    # D8 outputs to JAR files today rather than DEX files as DX used
    # to. To compensate, we extract the DEX from d8's output to meet the
    # expectations of make_dex callers.
    with tempfile.TemporaryDirectory(dir=".") as tmp_dir:
      zipfile.ZipFile(d8_output, "r").extractall(tmp_dir)
      os.rename(path.join(tmp_dir, "classes.dex"), dex_output)


  # Merge all the dex files.
  # Skip non-existing files, but at least 1 file must exist.
  def make_dexmerge(*dex_files_to_merge):
    # Dex file that acts as the destination.
    dst_file = dex_files_to_merge[0]

    # Skip any non-existing files.
    dex_files_to_merge = list(filter(path.exists, dex_files_to_merge))

    # NB: We merge even if there is just single input.
    # It is useful to normalize non-deterministic smali output.

    tmp_dir = "dexmerge"
    os.makedirs(tmp_dir)
    d8(["--min-api", api_level, "--output", tmp_dir] + dex_files_to_merge)
    assert not path.exists(path.join(tmp_dir, "classes2.dex"))
    for input_dex in dex_files_to_merge:
      os.remove(input_dex)
    os.rename(path.join(tmp_dir, "classes.dex"), dst_file)
    os.rmdir(tmp_dir)


  def make_hiddenapi(*dex_files):
    args = ["encode"]
    for dex_file in dex_files:
      args.extend(["--input-dex=" + dex_file, "--output-dex=" + dex_file])
    args.append("--api-flags=hiddenapi-flags.csv")
    args.append("--no-force-assign-all")
    hiddenapi(args)


  if path.exists("classes.dex"):
    zip(TEST_NAME + ".jar", "classes.dex")
    return


  def has_multidex():
    return HAS_SRC_MULTIDEX or HAS_JASMIN_MULTIDEX or HAS_SMALI_MULTIDEX


  def add_to_cp_args(old_cp_args, path):
    if len(old_cp_args) == 0:
      return ["-cp", path]
    else:
      return ["-cp", old_cp_args[1] + ":" + path]


  src_tmp_all = []

  if HAS_JASMIN:
    make_jasmin("jasmin_classes", find("jasmin", "*.j"))
    src_tmp_all = add_to_cp_args(src_tmp_all, "jasmin_classes")

  if HAS_JASMIN_MULTIDEX:
    make_jasmin("jasmin_classes2", find("jasmin-multidex", "*.j"))
    src_tmp_all = add_to_cp_args(src_tmp_all, "jasmin_classes2")

  if HAS_SRC and (HAS_SRC_MULTIDEX or HAS_SRC_AOTEX or HAS_SRC_BCPEX or
                  HAS_SRC_EX or HAS_SRC_ART or HAS_SRC2 or HAS_SRC_EX2):
    # To allow circular references, compile src/, src-multidex/, src-aotex/,
    # src-bcpex/, src-ex/ together and pass the output as class path argument.
    # Replacement sources in src-art/, src2/ and src-ex2/ can replace symbols
    # used by the other src-* sources we compile here but everything needed to
    # compile the other src-* sources should be present in src/ (and jasmin*/).
    os.makedirs("classes-tmp-all")
    javac_with_bootclasspath(["-implicit:none"] + src_tmp_all +
                             ["-d", "classes-tmp-all"] +
                             find("src", "*.java") +
                             find("src-multidex", "*.java") +
                             find("src-aotex", "*.java") +
                             find("src-bcpex", "*.java") +
                             find("src-ex", "*.java"))
    src_tmp_all = add_to_cp_args(src_tmp_all, "classes-tmp-all")

  if HAS_SRC_AOTEX:
    os.makedirs("classes-aotex")
    javac_with_bootclasspath(["-implicit:none"] + src_tmp_all +
                             ["-d", "classes-aotex"] +
                             find("src-aotex", "*.java"))
    if NEED_DEX:
      make_dex("classes-aotex")
      # rename it so it shows up as "classes.dex" in the zip file.
      os.rename("classes-aotex.dex", "classes.dex")
      zip(TEST_NAME + "-aotex.jar", "classes.dex")

  if HAS_SRC_BCPEX:
    os.makedirs("classes-bcpex")
    javac_with_bootclasspath(["-implicit:none"] + src_tmp_all +
                             ["-d", "classes-bcpex"] +
                             find("src-bcpex", "*.java"))
    if NEED_DEX:
      make_dex("classes-bcpex")
      # rename it so it shows up as "classes.dex" in the zip file.
      os.rename("classes-bcpex.dex", "classes.dex")
      zip(TEST_NAME + "-bcpex.jar", "classes.dex")

  if HAS_SRC:
    os.makedirs("classes", exist_ok=True)
    javac_with_bootclasspath(["-implicit:none"] + src_tmp_all +
                             ["-d", "classes"] + find("src", "*.java"))

  if HAS_SRC_ART:
    os.makedirs("classes", exist_ok=True)
    javac_with_bootclasspath(["-implicit:none"] + src_tmp_all +
                             ["-d", "classes"] + find("src-art", "*.java"))

  if HAS_SRC_MULTIDEX:
    os.makedirs("classes2")
    javac_with_bootclasspath(["-implicit:none"] + src_tmp_all +
                             ["-d", "classes2"] +
                             find("src-multidex", "*.java"))
    if NEED_DEX:
      make_dex("classes2")

  if HAS_SRC2:
    os.makedirs("classes", exist_ok=True)
    javac_with_bootclasspath(["-implicit:none"] + src_tmp_all +
                             ["-d", "classes"] +
                             find("src2", "*.java"))

  # If the classes directory is not-empty, package classes in a DEX file.
  # NB: some tests provide classes rather than java files.
  if find("classes", "*"):
    if NEED_DEX:
      make_dex("classes")

  if HAS_JASMIN:
    # Compile Jasmin classes as if they were part of the classes.dex file.
    if NEED_DEX:
      make_dex("jasmin_classes")
      make_dexmerge("classes.dex", "jasmin_classes.dex")
    else:
      # Move jasmin classes into classes directory so that they are picked up
      # with -cp classes.
      os.makedirs("classes", exist_ok=True)
      shutil.copytree("jasmin_classes", "classes", dirs_exist_ok=True)

  if HAS_SMALI and NEED_DEX:
    # Compile Smali classes
    smali(["-JXmx512m", "assemble"] + SMALI_ARGS +
          ["--output", "smali_classes.dex"] + find("smali", "*.smali"))
    assert path.exists("smali_classes.dex")
    # Merge smali files into classes.dex,
    # this takes priority over any jasmin files.
    make_dexmerge("classes.dex", "smali_classes.dex")

  # Compile Jasmin classes in jasmin-multidex as if they were part of
  # the classes2.jar
  if HAS_JASMIN_MULTIDEX:
    if NEED_DEX:
      make_dex("jasmin_classes2")
      make_dexmerge("classes2.dex", "jasmin_classes2.dex")
    else:
      # Move jasmin classes into classes2 directory so that
      # they are picked up with -cp classes2.
      os.makedirs("classes2", exist_ok=True)
      shutil.copytree("jasmin_classes2", "classes2", dirs_exist_ok=True)
      shutil.rmtree("jasmin_classes2")

  if HAS_SMALI_MULTIDEX and NEED_DEX:
    # Compile Smali classes
    smali(["-JXmx512m", "assemble"] + SMALI_ARGS +
          ["--output", "smali_classes2.dex"] + find("smali-multidex", "*.smali"))

    # Merge smali_classes2.dex into classes2.dex
    make_dexmerge("classes2.dex", "smali_classes2.dex")

  if HAS_SRC_EX:
    os.makedirs("classes-ex", exist_ok=True)
    javac_with_bootclasspath(["-implicit:none"] + src_tmp_all +
                             ["-d", "classes-ex"] + find("src-ex", "*.java"))

  if HAS_SRC_EX2:
    os.makedirs("classes-ex", exist_ok=True)
    javac_with_bootclasspath(["-implicit:none"] + src_tmp_all +
                             ["-d", "classes-ex"] + find("src-ex2", "*.java"))

  if path.exists("classes-ex") and NEED_DEX:
    make_dex("classes-ex")

  if HAS_SMALI_EX and NEED_DEX:
    # Compile Smali classes
    smali(["-JXmx512m", "assemble"] + SMALI_ARGS +
          ["--output", "smali_classes-ex.dex"] + find("smali-ex", "*.smali"))
    assert path.exists("smali_classes-ex.dex")
    # Merge smali files into classes-ex.dex.
    make_dexmerge("classes-ex.dex", "smali_classes-ex.dex")

  if path.exists("classes-ex.dex"):
    # Apply hiddenapi on the dex files if the test has API list file(s).
    if use_hiddenapi and HAS_HIDDENAPI_SPEC:
      make_hiddenapi("classes-ex.dex")

    # quick shuffle so that the stored name is "classes.dex"
    os.rename("classes.dex", "classes-1.dex")
    os.rename("classes-ex.dex", "classes.dex")
    zip(TEST_NAME + "-ex.jar", "classes.dex")
    os.rename("classes.dex", "classes-ex.dex")
    os.rename("classes-1.dex", "classes.dex")

  # Apply hiddenapi on the dex files if the test has API list file(s).
  if NEED_DEX and use_hiddenapi and HAS_HIDDENAPI_SPEC:
    if has_multidex():
      make_hiddenapi("classes.dex", "classes2.dex")
    else:
      make_hiddenapi("classes.dex")

  # Create a single dex jar with two dex files for multidex.
  if NEED_DEX:
    if path.exists("classes2.dex"):
      zip(TEST_NAME + ".jar", "classes.dex", "classes2.dex")
    else:
      zip(TEST_NAME + ".jar", "classes.dex")