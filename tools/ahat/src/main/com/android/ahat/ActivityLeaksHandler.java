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

import com.android.ahat.heapdump.AhatClassObj;
import com.android.ahat.heapdump.AhatInstance;
import com.android.ahat.heapdump.AhatSnapshot;
import com.android.ahat.heapdump.Size;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

/**
 * Handler for the Activity leaks page.
 * <p>
 * This page displays a list of likely leaked Android Activities.
 * An Activity is considered leaked if it is strongly reachable and its
 * `mDestroyed` field is true.
 */
class ActivityLeaksHandler implements AhatHandler {
  private AhatSnapshot mSnapshot;

  private static class LeakInfo {
    final AhatClassObj cls;
    List<AhatInstance> instances;

    LeakInfo(AhatClassObj cls, List<AhatInstance> instances) {
      this.cls = cls;
      this.instances = instances;
    }
  }

  public ActivityLeaksHandler(AhatSnapshot snapshot) {
    mSnapshot = snapshot;
  }

  @Override
  public void handle(Doc doc, Query query) throws IOException {
    List<AhatInstance> leaks = mSnapshot.getActivityLeaks();
    String className = query.get("class", null);
    if (className != null) {
      List<AhatInstance> instances = new ArrayList<>();
      for (AhatInstance inst : leaks) {
        if (inst.getClassName().equals(className)) {
          instances.add(inst);
        }
      }
      if (!instances.isEmpty()) {
        printLeakInstances(doc, query, instances);
      } else {
        doc.println(DocString.text("No leaks found for class: " + className));
      }
    } else {
      doc.title("Activity Leaks");
      printActivityLeaks(doc, query);
    }
  }

  private void printActivityLeaks(Doc doc, Query query) {
    List<AhatInstance> leaks = mSnapshot.getActivityLeaks();
    if (leaks.isEmpty()) {
      doc.println(DocString.text("(no leaks found)"));
      return;
    }

    // Group leaks by class.
    Map<AhatClassObj, List<AhatInstance>> leaksByClass = new TreeMap<>(new Comparator<AhatClassObj>() {
      @Override
      public int compare(AhatClassObj o1, AhatClassObj o2) {
        return o1.getName().compareTo(o2.getName());
      }
    });
    for (AhatInstance inst : leaks) {
      leaksByClass.computeIfAbsent(inst.getClassObj(), k -> new ArrayList<>()).add(inst);
    }

    List<LeakInfo> leakInfos = new ArrayList<>();
    for (Map.Entry<AhatClassObj, List<AhatInstance>> entry : leaksByClass.entrySet()) {
      leakInfos.add(new LeakInfo(entry.getKey(), entry.getValue()));
    }

    Comparator<LeakInfo> classCompare = new Comparator<LeakInfo>() {
      @Override
      public int compare(LeakInfo i1, LeakInfo i2) {
        return i1.cls.getName().compareTo(i2.cls.getName());
      }
    };

    Comparator<LeakInfo> countCompare = new Comparator<LeakInfo>() {
      @Override
      public int compare(LeakInfo i1, LeakInfo i2) {
        return Integer.compare(i1.instances.size(), i2.instances.size());
      }
    };

    Sorter<LeakInfo> sorter = new Sorter<LeakInfo>(query, classCompare);
    sorter.addKey("class", classCompare);
    sorter.addKey("count", countCompare);
    sorter.sort(leakInfos);

    doc.table(
        new Column(sorter.link("class", "Class"), Column.Align.LEFT),
        new Column(sorter.link("count", "Count"), Column.Align.RIGHT)
    );

    for (LeakInfo info : leakInfos) {
      doc.row(
          DocString.link(
              DocString.formattedUri("activity-leaks?class=%s", info.cls.getName()),
              DocString.text(info.cls.getName())),
          DocString.format("%,d", info.instances.size())
      );
    }
    doc.end();
  }

  private void printLeakInstances(Doc doc, Query query, List<AhatInstance> instances) {
    doc.title("Activity Leak Instances");
    doc.description(DocString.text("Class"), DocString.text(instances.get(0).getClassName()));
    doc.end();

    SizeTable.table(doc, mSnapshot.isDiffed(),
        new Column("Heap"),
        new Column("Object"));

    SubsetSelector<AhatInstance> selector = new SubsetSelector<>(query, "activity-leaks", instances);
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
