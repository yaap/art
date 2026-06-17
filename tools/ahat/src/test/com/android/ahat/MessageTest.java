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

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import com.android.ahat.heapdump.AhatInstance;
import com.android.ahat.heapdump.AhatMessageInstance;
import com.android.ahat.heapdump.AhatSnapshot;
import java.io.IOException;
import org.junit.Test;

public class MessageTest {
  @Test
  public void messageAnalysis() throws IOException {
    TestDump dump = TestDump.getTestDump();
    AhatSnapshot snapshot = dump.getAhatSnapshot();

    // Verify Message1
    AhatInstance obj = dump.getDumpedAhatInstance("message1");
    assertNotNull(obj);
    assertTrue(obj.isMessageInstance());
    AhatMessageInstance msg1 = obj.asMessageInstance();
    assertEquals(1000, msg1.getWhen());
    assertEquals(42, msg1.getWhat());
    assertEquals(0, msg1.getArg1());
    assertEquals(0, msg1.getArg2());
    assertNull(msg1.getObj());
    assertNull(msg1.getCallback());
    assertFalse(msg1.isSyncBarrier());

    // Verify Looper association
    AhatInstance looper = dump.getDumpedAhatInstance("looper");
    AhatInstance handler = dump.getDumpedAhatInstance("handler");
    assertEquals(looper, msg1.getLooper());
    assertEquals(handler, msg1.getTarget());

    // Verify Queue order
    AhatMessageInstance barrier = msg1.getNextInQueue();
    assertNotNull(barrier);
    assertTrue(barrier.isSyncBarrier());
    assertEquals(1500, barrier.getWhen());
    assertEquals(123, barrier.getArg1());
    assertNull(barrier.getTarget());
    assertEquals(msg1, barrier.getPrev());

    AhatMessageInstance msg2 = barrier.getNextInQueue();
    assertNotNull(msg2);
    assertEquals(2000, msg2.getWhen());
    assertEquals(43, msg2.getWhat());
    assertEquals(0, msg2.getArg1());
    assertEquals(0, msg2.getArg2());
    assertNull(msg2.getObj());
    assertNull(msg2.getCallback());
    assertFalse(msg2.isSyncBarrier());
    assertEquals(handler, msg2.getTarget());
    assertEquals(barrier, msg2.getPrev());

    assertNull(msg2.getNextInQueue());

    // Verify Dump Data
    AhatMessageInstance.MessageDumpData data = snapshot.getMessageDumpData();
    assertNotNull(data);
    assertFalse(data.loopers.isEmpty());

    boolean found = false;
    for (AhatMessageInstance.MessageDumpData.LooperInfo info : data.loopers) {
      if (info.looper.equals(looper)) {
        found = true;
        assertEquals(3, info.messages.size());
        assertEquals(msg1, info.messages.get(0));
        assertEquals(barrier, info.messages.get(1));
        assertEquals(msg2, info.messages.get(2));
      }
    }
    assertTrue("Looper not found in analysis", found);
  }
}
