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

import com.android.ahat.heapdump.AhatMessageInstance;
import com.android.ahat.heapdump.AhatSnapshot;
import java.io.IOException;

class LoopersHandler implements AhatHandler {
  private AhatSnapshot mSnapshot;

  public LoopersHandler(AhatSnapshot snapshot) {
    mSnapshot = snapshot;
  }

  @Override
  public void handle(Doc doc, Query query) throws IOException {
    doc.title("Loopers");

    AhatMessageInstance.MessageDumpData data = mSnapshot.getMessageDumpData();
    if (data == null || data.loopers.isEmpty()) {
      doc.println(DocString.text("(no loopers)"));
      return;
    }

    doc.table(
        new Column("Looper"),
        new Column("Name"),
        new Column("Messages", Column.Align.RIGHT),
        new Column("Earliest When", Column.Align.RIGHT),
        new Column("Latest When", Column.Align.RIGHT));

    for (AhatMessageInstance.MessageDumpData.LooperInfo info : data.loopers) {
      AhatMessageInstance first = info.messages.isEmpty() ? null : info.messages.get(0);
      AhatMessageInstance last =
          info.messages.isEmpty() ? null : info.messages.get(info.messages.size() - 1);

      DocString earliestWhenStr = DocString.text("-");
      if (first != null) {
        earliestWhenStr = DocString.duration(first.getWhen() - mSnapshot.getUptimeMillis());
        earliestWhenStr = DocString.link(
            DocString.formattedUri("object?id=0x%x", first.getId()), earliestWhenStr);
      }
      DocString latestWhenStr = DocString.text("-");
      if (last != null) {
        latestWhenStr = DocString.duration(last.getWhen() - mSnapshot.getUptimeMillis());
        latestWhenStr = DocString.link(
            DocString.formattedUri("object?id=0x%x", last.getId()), latestWhenStr);
      }

      doc.row(
          Summarizer.summarize(info.looper),
          DocString.text(info.threadName),
          DocString.format("%,d", info.messages.size()),
          earliestWhenStr,
          latestWhenStr);
    }
    doc.end();
  }
}
