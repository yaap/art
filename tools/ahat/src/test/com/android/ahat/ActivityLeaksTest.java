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

import com.android.ahat.heapdump.AhatInstance;
import com.android.ahat.heapdump.AhatSnapshot;
import com.android.ahat.heapdump.Reachability;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import org.junit.Test;
import static org.junit.Assert.assertEquals;

public class ActivityLeaksTest {
  @Test
  public void activityLeaks() throws IOException {
    TestDump dump = TestDump.getTestDump();
    AhatSnapshot snapshot = dump.getAhatSnapshot();
    List<AhatInstance> leaks = snapshot.getActivityLeaks();

    // We expect the MyActivity instances to be leaked.
    List<String> leakNames = new ArrayList<>();
    for (AhatInstance leak : leaks) {
      leakNames.add(leak.getClassName());
    }
    assertEquals(Arrays.asList("DumpedStuff$MyActivity", "DumpedStuff$MyActivity"), leakNames);
  }
}
