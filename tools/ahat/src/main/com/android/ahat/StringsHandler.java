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
import com.android.ahat.heapdump.Reachability;
import com.android.ahat.heapdump.Reference;
import com.android.ahat.heapdump.Size;
import com.android.ahat.heapdump.Sort;
import java.io.IOException;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.HashSet;
import java.util.List;
import java.util.Queue;
import java.util.Set;

class StringsHandler implements AhatHandler {
  private static final String STRINGS_ID = "strings";

  private AhatSnapshot mSnapshot;

  private static class DuplicateStringInfo {
    final int index;
    final AhatInstance representative;
    final int length;
    final int count;
    final Size totalSize;
    final String heapName;

    DuplicateStringInfo(int index, List<AhatInstance> instances, Reachability unusedRetained) {
      this.index = index;
      this.representative = instances.get(0);
      String value = representative.asString();
      this.length = value == null ? 0 : value.length();
      this.count = instances.size();
      this.heapName = representative.getHeap().getName();

      // To calculate the total size of this group of duplicate strings, we sum the shallow size
      // of each String instance and the shallow size of its underlying 'value' array. We use a
      // HashSet to ensure each object is only counted once, which is important if multiple String
      // instances share the same 'value' array.
      Set<AhatInstance> counted = new HashSet<>();
      Size size = Size.ZERO;
      for (AhatInstance inst : instances) {
        if (counted.add(inst)) {
          size = size.plus(inst.getSize());
        }
        AhatInstance valueArray = inst.getRefField("value");
        if (valueArray != null && counted.add(valueArray)) {
          size = size.plus(valueArray.getSize());
        }
      }
      this.totalSize = size;
    }
  }


  public StringsHandler(AhatSnapshot snapshot) {
    mSnapshot = snapshot;
  }

  @Override
  public void handle(Doc doc, Query query) throws IOException {
    int id = query.getInt("id", -1);
    List<List<AhatInstance>> duplicates = mSnapshot.getDuplicateStrings();
    if (id >= 0 && duplicates != null && id < duplicates.size()) {
      printStringInstances(doc, query, duplicates.get(id));
    } else {
      doc.title("Strings");

      doc.section("Duplicate Strings");
      printDuplicateStrings(doc, query);

      doc.section("All Strings");
      doc.println(DocString.link(
          DocString.uri("objects?class=java.lang.String"),
          DocString.text("All Strings")));
    }
  }

  private void printDuplicateStrings(Doc doc, Query query) {
    List<List<AhatInstance>> duplicates = mSnapshot.getDuplicateStrings();
    if (duplicates == null || duplicates.isEmpty()) {
       doc.println(DocString.text("(no duplicates)"));
       return;
    }

    List<DuplicateStringInfo> duplicateInfos = new ArrayList<>(duplicates.size());
    Reachability retained = mSnapshot.getRetainedReachability();
    for (int i = 0; i < duplicates.size(); ++i) {
      duplicateInfos.add(new DuplicateStringInfo(i, duplicates.get(i), retained));
    }

    Comparator<DuplicateStringInfo> lengthCompare = new Comparator<DuplicateStringInfo>() {
      @Override
      public int compare(DuplicateStringInfo i1, DuplicateStringInfo i2) {
        return Integer.compare(i1.length, i2.length);
      }
    };

    Comparator<DuplicateStringInfo> countCompare = new Comparator<DuplicateStringInfo>() {
      @Override
      public int compare(DuplicateStringInfo i1, DuplicateStringInfo i2) {
        return Integer.compare(i1.count, i2.count);
      }
    };

    Comparator<DuplicateStringInfo> totalSizeCompare = new Comparator<DuplicateStringInfo>() {
      @Override
      public int compare(DuplicateStringInfo i1, DuplicateStringInfo i2) {
        return i1.totalSize.compareTo(i2.totalSize);
      }
    };

    Comparator<DuplicateStringInfo> heapCompare = new Comparator<DuplicateStringInfo>() {
      @Override
      public int compare(DuplicateStringInfo i1, DuplicateStringInfo i2) {
        return i1.heapName.compareTo(i2.heapName);
      }
    };

    Sorter<DuplicateStringInfo> sorter = new Sorter<DuplicateStringInfo>(
        query, totalSizeCompare.reversed());
    sorter.addKey("len", lengthCompare);
    sorter.addKey("count", countCompare);
    sorter.addKey("total", totalSizeCompare);
    sorter.addKey("heap", heapCompare);
    sorter.sort(duplicateInfos);

    SubsetSelector<DuplicateStringInfo> selector =
        new SubsetSelector<>(query, STRINGS_ID, duplicateInfos);

    doc.table(
        new Column(sorter.link("len", "Length"), Column.Align.RIGHT),
        new Column(sorter.link("count", "Count"), Column.Align.RIGHT),
        new Column(sorter.link("total", "Total Size"), Column.Align.RIGHT),
        new Column(sorter.link("heap", "Heap"), Column.Align.LEFT),
        new Column("Value", Column.Align.LEFT)
    );

    for (DuplicateStringInfo info : selector.selected()) {
        doc.row(
            DocString.format("%,d", info.length),
            DocString.link(
                DocString.formattedUri("strings?id=%d", info.index),
                DocString.format("%,d", info.count)),
            DocString.format("%,d", info.totalSize.getSize()),
            DocString.text(info.heapName),
            Summarizer.summarizeString(info.representative)
        );
    }
    doc.end();
    selector.render(doc);
  }


  private void printStringInstances(Doc doc, Query query, List<AhatInstance> instances) {
    doc.title("Duplicate String Instances");
    doc.description(DocString.text("Value"), Summarizer.summarizeString(instances.get(0)));
    doc.end();

    SizeTable.table(doc, mSnapshot.isDiffed(),
        new Column("Heap"),
        new Column("Object"));

    SubsetSelector<AhatInstance> selector = new SubsetSelector<>(query, STRINGS_ID, instances);
    for (AhatInstance inst : selector.selected()) {
      AhatInstance base = inst.getBaseline();
      SizeTable.row(doc,
          inst.getTotalRetainedSize(), base.getTotalRetainedSize(),
          DocString.text(inst.getHeap().getName()),
          Summarizer.summarize(inst));
    }
    SizeTable.end(doc);
    selector.render(doc);
  }
}
