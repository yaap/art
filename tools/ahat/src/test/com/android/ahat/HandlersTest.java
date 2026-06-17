/*
 * Copyright (C) 2025 The Android Open Source Project
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

import com.android.ahat.heapdump.AhatSnapshot;
import com.android.ahat.heapdump.Reachability;
import java.io.File;
import java.io.IOException;
import org.junit.Test;

public class HandlersTest {
  @Test
  public void rootPathNoCrash() throws IOException {
    AhatSnapshot snapshot = TestDump.getTestDump().getAhatSnapshot();
    AhatHandler handler = new OverviewHandler(snapshot,
        new File("my.hprof.file"),
        new File("my.base.hprof.file"));
    TestHandler.testNoCrash(handler, "http://localhost:7100");
  }

  @Test
  public void overviewNoCrash() throws IOException {
    AhatSnapshot snapshot = TestDump.getTestDump().getAhatSnapshot();
    AhatHandler handler = new OverviewHandler(
            snapshot, new File("my.hprof.file"), new File("my.base.hprof.file"));
    TestHandler.testNoCrash(handler, "http://localhost:7100/overview");
  }

  @Test
  public void rootedNoCrash() throws IOException {
    AhatSnapshot snapshot = TestDump.getTestDump().getAhatSnapshot();
    AhatHandler handler = new RootedHandler(snapshot);
    TestHandler.testNoCrash(handler, "http://localhost:7100/rooted");
  }

  @Test
  public void allocationsNoCrash() throws IOException {
    AhatSnapshot snapshot = TestDump.getTestDump().getAhatSnapshot();
    AhatHandler handler = new SiteHandler(snapshot);
    TestHandler.testNoCrash(handler, "http://localhost:7100/sites");
  }

  @Test
  public void bitmapsNoCrash() throws IOException {
    AhatSnapshot snapshot = TestDump.getTestDump().getAhatSnapshot();
    AhatHandler handler = new BitmapsHandler(snapshot);
    TestHandler.testNoCrash(handler, "http://localhost:7100/bitmaps");
  }

  @Test
  public void loopersNoCrash() throws IOException {
    AhatSnapshot snapshot = TestDump.getTestDump().getAhatSnapshot();
    AhatHandler handler = new LoopersHandler(snapshot);
    TestHandler.testNoCrash(handler, "http://localhost:7100/loopers");
  }

  @Test
  public void stringsNoCrash() throws IOException {
    AhatSnapshot snapshot = TestDump.getTestDump().getAhatSnapshot();
    AhatHandler handler = new StringsHandler(snapshot);
    TestHandler.testNoCrash(handler, "http://localhost:7100/strings");
  }

  @Test
  public void activityLeaksNoCrash() throws IOException {
    AhatSnapshot snapshot = TestDump.getTestDump().getAhatSnapshot();
    AhatHandler handler = new ActivityLeaksHandler(snapshot);
    TestHandler.testNoCrash(handler, "http://localhost:7100/activity-leaks");
  }
}
