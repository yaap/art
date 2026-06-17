#!/bin/bash
#
# Copyright (C) 2025 The Android Open Source Project
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


def run(ctx, args):

  if (args.switch_interpreter or "--debuggable" in args.Xcompiler_option):
    # On debuggable runtimes we disable oat code and also compile methods with
    # tracing support. So the output is slightly different. Switch interpreter
    # also supports method tracing.
    ctx.expected_stdout = ctx.expected_stdout.with_suffix(".debuggable.txt")

  ctx.default_run(args)
