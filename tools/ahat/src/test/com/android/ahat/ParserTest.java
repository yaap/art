/*
 * Copyright (C) 2026 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.android.ahat;

import com.android.ahat.heapdump.HprofFormatException;
import com.android.ahat.heapdump.Parser;
import com.android.ahat.proguard.ProguardMap;
import java.io.IOException;
import java.nio.ByteBuffer;
import org.junit.Test;

public class ParserTest {

  /**
   * Regression test for b/490200823 where AHAT stalls when reading an empty hprof.
   */
  @Test(expected = HprofFormatException.class)
  public void emptyHeapDump() throws IOException, HprofFormatException {
    Parser.parseHeapDump(ByteBuffer.allocate(0), new ProguardMap());
  }
}
