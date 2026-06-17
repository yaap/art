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

import com.android.ahat.heapdump.AhatBitmapInstance;
import com.android.ahat.heapdump.AhatInstance;
import com.android.ahat.heapdump.AhatSnapshot;
import com.android.ahat.heapdump.Size;
import com.android.ahat.heapdump.Sort;
import java.io.IOException;
import java.util.Collections;
import java.util.Comparator;
import java.util.List;

class BitmapsHandler implements AhatHandler {
  private AhatSnapshot mSnapshot;

  public BitmapsHandler(AhatSnapshot snapshot) {
    mSnapshot = snapshot;
  }

  @Override
  public void handle(Doc doc, Query query) throws IOException {
    doc.title("Bitmaps");

    doc.section("Duplicate Bitmaps");
    printDuplicateBitmaps(doc, query);

    doc.section("All Bitmaps");
    printAllBitmaps(doc);
  }

  private void printDuplicateBitmaps(Doc doc, Query query) {
    List<List<AhatBitmapInstance>> duplicates = mSnapshot.findDuplicateBitmaps();
    if (duplicates == null || duplicates.isEmpty()) {
       doc.println(DocString.text("(no duplicates)"));
       return;
    }

    Comparator<List<AhatBitmapInstance>> sizeCompare =
        Comparator.comparingLong(l -> l.get(0).getSize().getSize());

    Comparator<List<AhatBitmapInstance>> countCompare =
        Comparator.comparingInt(List::size);

    Comparator<List<AhatBitmapInstance>> sizeCountCompare =
        Comparator.comparingLong(l -> l.get(0).getSize().getSize() * l.size());

    Comparator<List<AhatBitmapInstance>> heapCompare =
        Comparator.comparing(l -> l.get(0).getHeap().getName());

    Sorter<List<AhatBitmapInstance>> sorter = new Sorter<List<AhatBitmapInstance>>(
        query, sizeCountCompare.reversed());
    sorter.addKey("size", sizeCompare);
    sorter.addKey("count", countCompare);
    sorter.addKey("sc", sizeCountCompare);
    sorter.addKey("heap", heapCompare);
    sorter.sort(duplicates);

    doc.table(
        new Column(sorter.link("size", "Size"), Column.Align.RIGHT),
        new Column(sorter.link("count", "Count"), Column.Align.RIGHT),
        new Column(sorter.link("sc", "Size * Count"), Column.Align.RIGHT),
        new Column(sorter.link("heap", "Heap"), Column.Align.LEFT),
        new Column("Preview"),
        new Column("Bitmaps")
    );

    for (List<AhatBitmapInstance> list : duplicates) {
        AhatBitmapInstance rep = list.get(0); // representative
        long size = rep.getSize().getSize();
        int count = list.size();

        DocString bitmaps = new DocString();
        for (AhatBitmapInstance inst : list) {
             bitmaps.append(Summarizer.summarize(inst)).append(" ");
        }

        doc.row(
            DocString.format("%,d", size),
            DocString.format("%,d", count),
            DocString.format("%,d", size * count),
            DocString.text(rep.getHeap().getName()),
            DocString.image(DocString.formattedUri("bitmap?id=0x%x", rep.getId()), "bitmap"),
            bitmaps
        );
    }
    doc.end();
  }

  private void printAllBitmaps(Doc doc) {
    doc.println(DocString.link(
        DocString.formattedUri("objects?id=0x%x&class=android.graphics.Bitmap",
            mSnapshot.getRootSite().getId()),
        DocString.text("View all bitmaps")));
  }
}
