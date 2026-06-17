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

import com.android.ahat.heapdump.AhatInstance;
import com.android.ahat.heapdump.AhatSnapshot;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import org.junit.Test;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

public class StringsTest {

  private static class MockDoc implements Doc {
    List<String> columns = new ArrayList<>();
    List<List<String>> rows = new ArrayList<>();

    @Override public void title(String format, Object... args) {}
    @Override public void menu(DocString string) {}
    @Override public void section(String title) {}
    @Override public void println(DocString string) {}
    @Override public void big(DocString string) {}
    @Override public void table(Column... columns) {
      for (Column c : columns) {
        if (c.heading != null) {
          this.columns.add(c.heading.html());
        } else {
          this.columns.add("");
        }
      }
    }
    @Override public void table(DocString description, List<Column> subcols, List<Column> cols) {}
    @Override public void row(DocString... values) {
      List<String> row = new ArrayList<>();
      for (DocString val : values) {
        row.add(val.html());
      }
      rows.add(row);
    }
    @Override public void descriptions() {}
    @Override public void description(DocString key, DocString value) {}
    @Override public void end() {}
    @Override public void close() {}
  }

  @Test
  public void testDuplicateStrings() throws IOException {
    TestDump dump = TestDump.getTestDump();
    AhatSnapshot snapshot = dump.getAhatSnapshot();

    // Verify duplicates are found
    List<List<AhatInstance>> duplicates = snapshot.getDuplicateStrings();
    assertFalse("Duplicate strings should be found", duplicates.isEmpty());

    boolean foundDuplicate = false;
    for (List<AhatInstance> list : duplicates) {
      if (list.size() >= 2) {
        String value = list.get(0).asString();
        if ("duplicate".equals(value)) {
          foundDuplicate = true;
          break;
        }
      }
    }
    assertTrue("Should find specific duplicate string 'duplicate'", foundDuplicate);
  }

  @Test
  public void testDuplicateStringsSize() throws IOException {
    TestDump dump = TestDump.getTestDump();
    AhatSnapshot snapshot = dump.getAhatSnapshot();
    StringsHandler handler = new StringsHandler(snapshot);
    MockDoc doc = new MockDoc();

    Query query = new Query(DocString.uri("strings"));
    handler.handle(doc, query);

    // Find the row for "duplicate"
    List<String> duplicateRow = null;
    for (List<String> row : doc.rows) {
      // The last column is "Value".
      String valueCol = row.get(row.size() - 1);
      if (valueCol.contains("duplicate")) {
        duplicateRow = row;
        break;
      }
    }
    assertTrue("Row for 'duplicate' string not found", duplicateRow != null);

    // Find the instances for "duplicate"
    List<AhatInstance> instances = null;
    for (List<AhatInstance> list : snapshot.getDuplicateStrings()) {
      if ("duplicate".equals(list.get(0).asString())) {
        instances = list;
        break;
      }
    }

    long expectedSize = 0;
    for (AhatInstance inst : instances) {
        expectedSize += inst.getTotalRetainedSize().getSize();
    }
    // Column index 2 is Total Size (Length, Count, Total Size, Heap, Value)
    String totalSizeStr = duplicateRow.get(2);
    long actualSize = Long.parseLong(totalSizeStr.replace(",", ""));

    assertEquals("Total size mismatch", expectedSize, actualSize);
  }
}
