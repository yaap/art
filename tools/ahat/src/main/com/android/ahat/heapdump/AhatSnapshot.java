/*
 * Copyright (C) 2016 The Android Open Source Project
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

package com.android.ahat.heapdump;

import com.android.ahat.dominators.Dominators;
import com.android.ahat.progress.Progress;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * A parsed heap dump.
 * It contains methods to access the heaps, allocation sites, roots, classes,
 * and instances from the parsed heap dump.
 */
public class AhatSnapshot implements Diffable<AhatSnapshot> {
  private final Site mRootSite;

  private final SuperRoot mSuperRoot;

  // List of all ahat instances.
  private final Instances<AhatInstance> mInstances;

  private List<AhatHeap> mHeaps;

  private AhatSnapshot mBaseline = this;

  private AhatBitmapInstance.BitmapDumpData mBitmapDumpData = null;
  private AhatMessageInstance.MessageDumpData mMessageDumpData = null;
  private List<List<AhatInstance>> mDuplicateStrings = null;
  private List<AhatInstance> mActivityLeaks = null;
  private Reachability mRetained;
  private long mUptimeMillis = 0;

  AhatSnapshot(SuperRoot root,
               Instances<AhatInstance> instances,
               List<AhatHeap> heaps,
               Site rootSite,
               Progress progress,
               Reachability retained,
               long uptimeMillis) {
    mSuperRoot = root;
    mInstances = instances;
    mHeaps = heaps;
    mRootSite = rootSite;
    mRetained = retained;
    mUptimeMillis = uptimeMillis;

    AhatInstance.computeReachability(mSuperRoot, mInstances, progress, mInstances.size());

    mBitmapDumpData = AhatBitmapInstance.findBitmapDumpData(mSuperRoot, mInstances);
    mMessageDumpData = AhatMessageInstance.findMessageDumpData(mInstances, progress, mInstances.size());
    mDuplicateStrings = findDuplicateStrings(mInstances, progress, retained);
    mActivityLeaks = findActivityLeaks(mInstances, progress);

    for (AhatInstance inst : mInstances) {
      // Add this instance to its site.
      inst.getSite().addInstance(inst);

      // Update registered native allocation size.
      AhatInstance.RegisteredNativeAllocation nra = inst.asRegisteredNativeAllocation();
      if (nra != null) {
        nra.referent.addRegisteredNativeSize(nra.size);
      }

      if (retained == Reachability.UNREACHABLE && inst.isUnreachable()) {
        if (inst.getSamplePath().size() == 1) {
          mSuperRoot.addRoot(inst);
        }
      }
    }

    new Dominators<AhatInstance>(new AhatGraph(retained))
        .progress(progress, mInstances.size())
        .computeDominators((AhatInstance) mSuperRoot);

    AhatInstance.computeRetainedSize(mSuperRoot, mHeaps.size());

    for (AhatHeap heap : mHeaps) {
      heap.addToSize(mSuperRoot.getRetainedSize(heap));
    }

    mRootSite.prepareForUse(0, mHeaps.size(), retained);
  }

  private static class AhatGraph implements Dominators.Graph<AhatInstance> {
    private final Reachability retained;

    AhatGraph(Reachability retained) {
      this.retained = retained;
    }

    @Override
    public void setDominatorsComputationState(AhatInstance node, Object state) {
      node.setTemporaryUserData(state);
    }

    @Override
    public Object getDominatorsComputationState(AhatInstance node) {
      return node.getTemporaryUserData();
    }

    @Override
    public Iterable<AhatInstance> getReferencesForDominators(AhatInstance node) {
      return node.getReferencesForDominators(retained);
    }

    @Override
    public void setDominator(AhatInstance node, AhatInstance dominator) {
      node.setDominator(dominator);
    }
  }

  /**
   * Returns the instance with the given id in this snapshot.
   * Where the id of an instance x is x.getId().
   * Returns null if no instance with the given id is found.
   *
   * @param id the id of the instance to find
   * @return the instance with the given id
   */
  public AhatInstance findInstance(long id) {
    return mInstances.get(id);
  }

  /**
   * Returns the AhatClassObj with the given id in this snapshot.
   * Where the id of a class object x is x.getId().
   * Returns null if no class object with the given id is found.
   *
   * @param id the id of the class object to find
   * @return the class object with the given id
   */
  public AhatClassObj findClassObj(long id) {
    AhatInstance inst = findInstance(id);
    return inst == null ? null : inst.asClassObj();
  }

  /**
   * Returns the heap with the given name.
   * Where the name of a heap x is x.getName().
   * Returns null if no heap with the given name could be found.
   *
   * @param name the name of the heap to get
   * @return the heap with the given name
   */
  public AhatHeap getHeap(String name) {
    // We expect a small number of heaps (maybe 3 or 4 total), so a linear
    // search should be acceptable here performance wise.
    for (AhatHeap heap : getHeaps()) {
      if (heap.getName().equals(name)) {
        return heap;
      }
    }
    return null;
  }

  /**
   * Returns a list of heaps in the snapshot in canonical order.
   * <p>
   * Note: modifications to the returned list are visible to this
   * AhatSnapshot, which is used by diff to insert placeholder heaps.
   *
   * @return list of heaps
   */
  public List<AhatHeap> getHeaps() {
    return mHeaps;
  }

  /**
   * Returns a collection of "rooted" instances.
   * An instance is "rooted" if it is a GC root, or if it is retained by more
   * than one GC root. These are reachable instances that are not immediately
   * dominated by any other instance in the heap.
   *
   * @return collection of rooted instances
   */
  public List<AhatInstance> getRooted() {
    return mSuperRoot.getDominated();
  }

  /**
   * Returns the root allocation site for this snapshot.
   *
   * @return the root allocation site
   */
  public Site getRootSite() {
    return mRootSite;
  }

  /**
   * Returns the site associated with the given id.
   * Where the id of a site x is x.getId().
   * Returns the root site if no site with the given id is found.
   *
   * @param id the id of the site to get
   * @return the site with the given id
   */
  public Site getSite(long id) {
    Site site = mRootSite.findSite(id);
    return site == null ? mRootSite : site;
  }

  void setBaseline(AhatSnapshot baseline) {
    mBaseline = baseline;
  }

  /**
   * Returns true if this snapshot has been diffed against a different
   * snapshot.
   *
   * @return true if the snapshot has been diffed
   */
  public boolean isDiffed() {
    return mBaseline != this;
  }

  @Override public AhatSnapshot getBaseline() {
    return mBaseline;
  }

  @Override public boolean isPlaceHolder() {
    return false;
  }

  /**
   * Returns duplicated bitmaps in this snapshot
   *
   * @return list of duplicated bitmaps
   */
  public List<List<AhatBitmapInstance>> findDuplicateBitmaps() {
    return AhatBitmapInstance.findDuplicates(mBitmapDumpData);
  }

  /**
   * Returns the message dump data.
   *
   * @return the message dump data
   */
  public AhatMessageInstance.MessageDumpData getMessageDumpData() {
    return mMessageDumpData;
  }

  /**
   * Returns the uptime millis reference.
   *
   * @return the uptime in milliseconds
   */
  public long getUptimeMillis() {
    return mUptimeMillis;
  }

  /**
   * Returns the duplicate strings in this snapshot.
   *
   * @return list of duplicate strings
   */
  public List<List<AhatInstance>> getDuplicateStrings() {
    return mDuplicateStrings;
  }

  /**
   * Returns activity leaks in this snapshot.
   * <p>
   * The returned list is never null. If there are no leaks, an empty list is
   * returned.
   *
   * @return list of activity leaks
   */
  public List<AhatInstance> getActivityLeaks() {
    return mActivityLeaks;
  }

  /**
   * Returns the reachability level that instances must have to be considered
   * retained in this snapshot.
   *
   * @return the reachability level for retained instances
   */
  public Reachability getRetainedReachability() {
    return mRetained;
  }

  private static List<List<AhatInstance>> findDuplicateStrings(
      Instances<AhatInstance> instances, Progress progress, Reachability retained) {
    progress.start("Analyzing strings", instances.size());
    Map<String, List<AhatInstance>> strings = new HashMap<>();
    for (AhatInstance inst : instances) {
      if (inst.isInstanceOfClass("java.lang.String") && inst.getReachability().notWeakerThan(retained)) {
        String value = inst.asString();
        if (value != null) {
          List<AhatInstance> list = strings.get(value);
          if (list == null) {
            list = new ArrayList<>();
            strings.put(value, list);
          }
          list.add(inst);
        }
      }
      progress.advance();
    }
    progress.done();

    List<List<AhatInstance>> duplicates = new ArrayList<>();
    for (List<AhatInstance> list : strings.values()) {
      if (list.size() > 1) {
        duplicates.add(list);
      }
    }
    return duplicates;
  }

  /**
   * Identifies likely activity leaks in the snapshot.
   * <p>
   * This method scans all strongly reachable instances in the heap dump. It looks for
   * classes that are subclasses of `android.app.Activity`. If an instance is found
   * to be strongly reachable and its `mDestroyed` field is true, it is added to the
   * list of leaks.
   *
   * @param instances the list of all instances in the heap dump
   * @param progress for reporting progress
   * @return a list of leaked activity instances
   */
  private static List<AhatInstance> findActivityLeaks(
      Instances<AhatInstance> instances, Progress progress) {
    progress.start("Analyzing activity leaks", instances.size());
    List<AhatInstance> leaks = new ArrayList<>();
    for (AhatInstance inst : instances) {
      progress.advance();

      // An activity can't be leaked if it isn't strongly reachable.
      if (!inst.isStronglyReachable()) {
        continue;
      }

      // Only look at instances of activities.
      if (inst.getClassObj() == null || !inst.getClassObj().isSubClassOf("android.app.Activity")) {
        continue;
      }

      // A non-destroyed activity is not considered a leak.
      Value value = inst.getField("mDestroyed");
      if (value == null || !value.isBoolean() || !value.asBoolean()) {
        continue;
      }

      leaks.add(inst);
    }
    progress.done();
    return leaks;
  }
}
