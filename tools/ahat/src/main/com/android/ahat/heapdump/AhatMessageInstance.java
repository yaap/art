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

package com.android.ahat.heapdump;

import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import com.android.ahat.progress.Progress;
import java.util.Map;
import java.util.Set;

/**
 * AhatInstance representing an android.os.Message object.
 */
public class AhatMessageInstance extends AhatClassInstance {
  /**
   * Constructs a new AhatMessageInstance.
   *
   * @param id the unique identifier for this instance.
   */
  AhatMessageInstance(long id) {
    super(id);
  }

  @Override
  public boolean isMessageInstance() {
    return true;
  }

  @Override
  public AhatMessageInstance asMessageInstance() {
    return this;
  }

  /**
   * Returns the timestamp when this message is scheduled to be delivered.
   *
   * @return the 'when' timestamp of the message.
   */
  public long getWhen() {
    return getLongField("when", 0L);
  }

  /**
   * Returns the handler that this message is targeted to.
   *
   * @return the target Handler instance, or null.
   */
  public AhatInstance getTarget() {
    return getRefField("target");
  }

  /**
   * Returns the callback runnable associated with this message.
   *
   * @return the callback Runnable instance, or null.
   */
  public AhatInstance getCallback() {
    return getRefField("callback");
  }

  /**
   * Returns the arg1 field of this message.
   *
   * @return the arg1 value.
   */
  public int getArg1() {
    return getIntField("arg1", 0);
  }

  /**
   * Returns the arg2 field of this message.
   *
   * @return the arg2 value.
   */
  public int getArg2() {
    return getIntField("arg2", 0);
  }

  /**
   * Returns the what field of this message.
   *
   * @return the what value.
   */
  public int getWhat() {
    return getIntField("what", 0);
  }

  /**
   * Returns the obj field of this message.
   *
   * @return the obj value, or null.
   */
  public AhatInstance getObj() {
    return getRefField("obj");
  }

  /**
   * Returns the insertSeq field of this message.
   *
   * @return the insertSeq value.
   */
  public long getInsertSeq() {
    return getLongField("insertSeq", 0L);
  }

  /**
   * Returns the next message linked from this message (raw field value).
   * Note: This is the 'next' field in the Message object, which might not
   * reflect the actual queue order if the heap dump captured a snapshot
   * while the list was being modified or if we are traversing a pool.
   *
   * @return the next Message instance, or null.
   */
  public AhatMessageInstance getNext() {
    AhatInstance next = getRefField("next");
    return next == null ? null : next.asMessageInstance();
  }

  /**
   * Returns true if this message is a synchronization barrier.
   * A sync barrier is characterized by a null target and null callback.
   *
   * @return true if this message is a sync barrier.
   */
  public boolean isSyncBarrier() {
    return getTarget() == null && getCallback() == null;
  }

  AhatInstance mLooper;
  AhatMessageInstance mPrev;
  AhatMessageInstance mNextInQueue;

  /**
   * Returns the Looper associated with this message.
   *
   * @return the Looper instance.
   */
  public AhatInstance getLooper() {
    return mLooper;
  }



  /**
   * Returns the previous message in the queue.
   *
   * @return the previous message.
   */
  public AhatMessageInstance getPrev() {
    return mPrev;
  }



  /**
   * Returns the next message in the queue order.
   *
   * @return the next message.
   */
  public AhatMessageInstance getNextInQueue() {
    return mNextInQueue;
  }

  /**
   * Data structure to hold aggregated information about messages and loopers.
   */
  public static class MessageDumpData {
    /**
     * Constructs a new MessageDumpData.
     */
    public MessageDumpData() {}

    /**
     * Information about a specific Looper and its message queue.
     */
    public static class LooperInfo {
      /** The Looper instance. */
      public AhatInstance looper;
      /** The Thread instance associated with the Looper. */
      public AhatInstance thread;
      /** The name of the thread. */
      public String threadName;
      /** The list of messages in the queue, ordered by execution time. */
      public List<AhatMessageInstance> messages = new ArrayList<>();
      /** The retained size of the messages in this queue. */
      public Size retainedSize = Size.ZERO;

      /**
       * Constructs a new LooperInfo.
       *
       * @param looper the Looper instance.
       * @param thread the Thread instance.
       * @param threadName the name of the thread.
       */
      public LooperInfo(AhatInstance looper, AhatInstance thread, String threadName) {
        this.looper = looper;
        this.thread = thread;
        this.threadName = threadName;
      }
    }

    /** List of all found Loopers. */
    public List<LooperInfo> loopers = new ArrayList<>();
  }

  /**
   * Analyzes the heap to find all Loopers and their associated Messages.
   *
   * @param instances the collection of all instances in the heap.
   * @param progress used to track progress of the traversal.
   * @param numInsts the number of instances, for tracking progress.
   * @return a MessageDumpData object containing the analysis results.
   */
  static MessageDumpData findMessageDumpData(
          Instances<AhatInstance> instances, Progress progress, long numInsts) {
    MessageDumpData data = new MessageDumpData();
    progress.start("Computing message dump", numInsts);

    for (AhatInstance inst : instances) {
      progress.advance();
      if (inst.isInstanceOfClass("android.os.Looper")) {
        AhatInstance queue = inst.getRefField("mQueue");
        AhatInstance thread = inst.getRefField("mThread");
        String threadName = (thread != null) ? thread.asString() : null;
        if (threadName == null && thread != null) {
          Value nameVal = thread.getField("name");
          if (nameVal != null && nameVal.isAhatInstance()) {
            threadName = nameVal.asAhatInstance().asString();
          }
        }
        if (threadName == null) {
          threadName = "???";
        }

        MessageDumpData.LooperInfo info = new MessageDumpData.LooperInfo(inst, thread, threadName);
        data.loopers.add(info);

        if (queue != null) {
          // Collect all messages from the queue.
          // The approach differs based on whether the queue is a legacy implementation
          // or a DeliQueue implementation. Since the choice of implementation is mutually exclusive
          // for any particular queue, we can just try both approaches and collect whatever is
          // found.
          Set<AhatMessageInstance> queueMessages = new HashSet<>();
          collectMessagesFromLegacyQueue(queue, queueMessages);
          collectMessagesFromConcurrentQueue(queue, queueMessages);

          Size retainedSize = Size.ZERO;
          for (AhatMessageInstance message : queueMessages) {
            message.mLooper = inst;
            info.messages.add(message);
            retainedSize = retainedSize.plus(message.getTotalRetainedSize());
          }
          info.retainedSize = retainedSize;

          Collections.sort(info.messages, (m1, m2) -> {
            // This logic is consistent with Message.compareMessages(m1, m2)
            int cmp = Long.compare(m1.getWhen(), m2.getWhen());
            if (cmp != 0) {
              return cmp;
            }
            Long seq1 = m1.getInsertSeq();
            Long seq2 = m2.getInsertSeq();
            return Long.compare(seq1, seq2);
          });

          AhatMessageInstance prev = null;
          for (AhatMessageInstance m : info.messages) {
            m.mPrev = prev;
            if (prev != null) {
              prev.mNextInQueue = m;
            }
            prev = m;
          }
        }
      }
    }
    progress.done();
    return data;
  }

  /**
   * Collects all messages from the queue, if it's a legacy implementation.
   *
   * @param queue the legacy message queue.
   * @param queueMessages the set of collected messages.
   */
  private static void collectMessagesFromLegacyQueue(
      AhatInstance queue, Set<AhatMessageInstance> queueMessages) {
    AhatInstance msg = queue.getRefField("mMessages");

    // Traverse the singly-linked list of messages.
    while (msg != null && msg.isMessageInstance()) {
      AhatMessageInstance message = msg.asMessageInstance();
      if (message.getLooper() != null) {
        break; // Avoid cycles if we revisit
      }
      if (!queueMessages.add(message)) {
        break;
      }
      msg = message.getRefField("next");
    }
  }

  /**
   * Collects all messages from the queue, if it's a DeliQueue implementation.
   *
   * @param queue the concurrent message queue.
   * @param queueMessages the set of collected messages.
   */
  private static void collectMessagesFromConcurrentQueue(
      AhatInstance queue, Set<AhatMessageInstance> queueMessages) {
    AhatInstance stack = queue.getRefField("mStack");

    if (stack != null) {
      // Collect all messages from the stack.
      AhatInstance top = stack.getRefField("mTopValue");
      while (top != null && top.isMessageInstance()) {
        AhatMessageInstance message = top.asMessageInstance();
        if (message.getLooper() != null) {
          break;
        }
        if (!queueMessages.add(message)) {
          break;
        }
        top = top.getRefField("next");
      }

      // Collect all messages from the heaps (sync and async).
      collectMessagesFromHeap(stack.getRefField("mSyncHeap"), queueMessages);
      collectMessagesFromHeap(stack.getRefField("mAsyncHeap"), queueMessages);

      // Intentionally don't collect messages from the freelist.
    }
  }

  private static void collectMessagesFromHeap(
      AhatInstance heap, Set<AhatMessageInstance> messages) {
    if (heap == null || !heap.isClassInstance()) {
      return;
    }
    AhatClassInstance heapClassInstance = heap.asClassInstance();

    Integer numElements = heapClassInstance.getIntField("mNumElements", 0);
    if (numElements == null || numElements == 0) {
      return;
    }

    AhatArrayInstance array = heapClassInstance.getArrayField("mHeap");
    if (array != null) {
      for (int i = 0; i < numElements; i++) {
        Value v = array.getValue(i);
        if (v != null && v.isAhatInstance()) {
          AhatInstance mi = v.asAhatInstance();
          if (mi.isMessageInstance()) {
            messages.add(mi.asMessageInstance());
          }
        }
      }
    }
  }
}
