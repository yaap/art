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

package com.android.server.art;

import static android.platform.test.flag.junit.DeviceFlagsValueProvider.createCheckFlagsRule;

import static com.android.server.art.PreRebootDexoptJob.JOB_ID;
import static com.android.server.art.testing.TestingUtils.FLAGS_PREFIX;

import static com.google.common.truth.Truth.assertThat;
import static com.google.common.truth.Truth.assertWithMessage;

import static org.mockito.Mockito.any;
import static org.mockito.Mockito.anyBoolean;
import static org.mockito.Mockito.anyInt;
import static org.mockito.Mockito.doAnswer;
import static org.mockito.Mockito.eq;
import static org.mockito.Mockito.isNull;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import android.app.job.JobInfo;
import android.app.job.JobParameters;
import android.app.job.JobScheduler;
import android.os.CancellationSignal;
import android.os.Process;
import android.os.SystemProperties;
import android.os.UpdateEngine;
import android.platform.test.annotations.RequiresFlagsDisabled;
import android.platform.test.annotations.RequiresFlagsEnabled;
import android.platform.test.flag.junit.CheckFlagsRule;

import androidx.test.filters.SmallTest;

import com.android.art.rw.flags.Flags;
import com.android.server.art.model.VerifyDexoptArtifactsResult;
import com.android.server.art.prereboot.PreRebootDriver;
import com.android.server.art.prereboot.PreRebootDriver.PreRebootResult;
import com.android.server.art.prereboot.PreRebootStatsReporter;
import com.android.server.art.proto.PreRebootStats.Status;
import com.android.server.art.testing.CommandExecution;
import com.android.server.art.testing.MockClock;
import com.android.server.art.testing.PreRebootStatsReporterHarness;
import com.android.server.art.testing.StaticMockitoRule;
import com.android.server.pm.PackageManagerLocal;

import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

import java.util.concurrent.Semaphore;
import java.util.concurrent.TimeUnit;
import java.util.stream.Collectors;
import java.util.stream.Stream;

@SmallTest
@RunWith(MockitoJUnitRunner.StrictStubs.class)
public class ArtShellCommandTest {
    private static final long TIMEOUT_SEC = 10;

    @Rule
    public StaticMockitoRule mockitoRule = new StaticMockitoRule(
            SystemProperties.class, BackgroundDexoptJobService.class, ArtJni.class);

    @Rule public final CheckFlagsRule checkFlagsRule = createCheckFlagsRule();

    @Mock private BackgroundDexoptJobService mJobService;
    @Mock private PreRebootDriver mPreRebootDriver;
    @Mock private JobScheduler mJobScheduler;
    @Mock private IArtd mArtd;
    @Mock private UpdateEngine mUpdateEngine;
    @Mock private PreRebootDexoptJob.Injector mPreRebootDexoptJobInjector;
    @Mock private ArtManagerLocal.Injector mArtManagerLocalInjector;
    @Mock private DexoptHelper mDexoptHelper;
    @Mock private PackageManagerLocal mPackageManagerLocal;
    @Mock private ArtShellCommand.Injector mInjector;

    private PreRebootDexoptJob mPreRebootDexoptJob;
    private ArtManagerLocal mArtManagerLocal;
    private JobInfo mJobInfo;
    private JobParameters mJobParameters;
    private PreRebootStatsReporterHarness mPreRebootStatsReporterHarness;
    private MockClock mMockClock;

    @Before
    public void setUp() throws Exception {
        mPreRebootStatsReporterHarness = new PreRebootStatsReporterHarness();
        mMockClock = new MockClock();

        lenient()
                .when(mPreRebootStatsReporterHarness.getInjector().getClock())
                .thenReturn(mMockClock);

        lenient()
                .when(SystemProperties.getBoolean(eq("dalvik.vm.enable_pr_dexopt"), anyBoolean()))
                .thenReturn(true);

        lenient().when(mJobScheduler.schedule(any())).thenAnswer(invocation -> {
            mJobInfo = invocation.<JobInfo>getArgument(0);
            mJobParameters = mock(JobParameters.class);
            assertThat(mJobInfo.getId()).isEqualTo(JOB_ID);
            lenient().when(mJobParameters.getExtras()).thenReturn(mJobInfo.getExtras());
            return JobScheduler.RESULT_SUCCESS;
        });

        lenient()
                .doAnswer(invocation -> {
                    mJobInfo = null;
                    mJobParameters = null;
                    return null;
                })
                .when(mJobScheduler)
                .cancel(JOB_ID);

        lenient().when(mJobScheduler.getPendingJob(JOB_ID)).thenAnswer(invocation -> {
            return mJobInfo;
        });

        lenient()
                .when(mPreRebootDexoptJobInjector.getPreRebootDriver())
                .thenReturn(mPreRebootDriver);
        lenient()
                .when(mPreRebootDexoptJobInjector.getStatsReporter())
                .thenReturn(mPreRebootStatsReporterHarness.createStatsReporter());
        lenient().when(mPreRebootDexoptJobInjector.getJobScheduler()).thenReturn(mJobScheduler);
        lenient().when(mPreRebootDexoptJobInjector.getArtd()).thenReturn(mArtd);
        lenient().when(mPreRebootDexoptJobInjector.getUpdateEngine()).thenReturn(mUpdateEngine);
        lenient().when(mPreRebootDexoptJobInjector.isAtLeastB()).thenReturn(true);
        lenient().when(mPreRebootDexoptJobInjector.getClock()).thenReturn(mMockClock);
        lenient()
                .when(mPreRebootDexoptJobInjector.getAsyncExecutor())
                .thenReturn(mMockClock.getAsyncExecutor());
        mPreRebootDexoptJob = new PreRebootDexoptJob(mPreRebootDexoptJobInjector);

        lenient().when(BackgroundDexoptJobService.getJob(JOB_ID)).thenReturn(mPreRebootDexoptJob);

        lenient()
                .when(mArtManagerLocalInjector.getPreRebootDexoptJob())
                .thenReturn(mPreRebootDexoptJob);
        lenient().when(mArtManagerLocalInjector.getDexoptHelper()).thenReturn(mDexoptHelper);
        mArtManagerLocal = new ArtManagerLocal(mArtManagerLocalInjector);

        lenient().when(mInjector.getArtManagerLocal()).thenReturn(mArtManagerLocal);
        lenient().when(mInjector.getPackageManagerLocal()).thenReturn(mPackageManagerLocal);
        lenient().when(mInjector.isVerificationSupported()).thenReturn(true);
        lenient().when(mInjector.getSleeper()).thenReturn(mMockClock);
    }

    @Test
    public void testOnOtaStagedPermission() throws Exception {
        when(mInjector.getCallingUid()).thenReturn(Process.SHELL_UID);

        try (var execution = new CommandExecution(
                     createHandler(), "art", "on-ota-staged", "--slot", "_b")) {
            int exitCode = execution.waitAndGetExitCode();
            String outputs = getOutputs(execution);
            assertWithMessage(outputs).that(exitCode).isEqualTo(-1);
            assertThat(outputs).contains("Only root can call 'on-ota-staged'");
        }
    }

    @Test
    public void testOnOtaStagedSync() throws Exception {
        when(mPreRebootDexoptJobInjector.isAtLeastB()).thenReturn(false);
        when(mInjector.getCallingUid()).thenReturn(Process.ROOT_UID);

        when(mPreRebootDriver.run(eq("_b"), eq(false) /* mapSnapshotsForOta */, any(),
                     eq(ReasonMapping.REASON_PRE_REBOOT_DEXOPT)))
                .thenReturn(new PreRebootResult(Status.STATUS_FINISHED));

        try (var execution = new CommandExecution(
                     createHandler(), "art", "on-ota-staged", "--slot", "_b")) {
            int exitCode = execution.waitAndGetExitCode();
            String outputs = getOutputs(execution);
            assertWithMessage(outputs).that(exitCode).isEqualTo(0);
            assertThat(outputs).contains("Job finished. See logs for details");
        }
    }

    @Test
    public void testOnOtaStagedSyncFatalError() throws Exception {
        when(mPreRebootDexoptJobInjector.isAtLeastB()).thenReturn(false);
        when(mInjector.getCallingUid()).thenReturn(Process.ROOT_UID);

        when(mPreRebootDriver.run(eq("_b"), eq(false) /* mapSnapshotsForOta */, any(), any()))
                .thenThrow(RuntimeException.class);

        try (var execution = new CommandExecution(
                     createHandler(), "art", "on-ota-staged", "--slot", "_b")) {
            int exitCode = execution.waitAndGetExitCode();
            String outputs = getOutputs(execution);
            assertWithMessage(outputs).that(exitCode).isEqualTo(0);
            assertThat(outputs).contains("Job encountered a fatal error");
        }
    }

    @Test
    public void testOnOtaStagedSyncCancelledByCommand() throws Exception {
        when(mPreRebootDexoptJobInjector.isAtLeastB()).thenReturn(false);
        when(mInjector.getCallingUid()).thenReturn(Process.ROOT_UID);

        when(mPreRebootDriver.run(eq("_b"), eq(false) /* mapSnapshotsForOta */, any(), any()))
                .thenAnswer(invocation -> {
                    Semaphore dexoptCancelled = new Semaphore(0 /* permits */);
                    var cancellationSignal = invocation.<CancellationSignal>getArgument(2);
                    cancellationSignal.setOnCancelListener(() -> dexoptCancelled.release());
                    assertThat(dexoptCancelled.tryAcquire(TIMEOUT_SEC, TimeUnit.SECONDS)).isTrue();
                    return new PreRebootResult(Status.STATUS_FINISHED);
                });

        try (var execution = new CommandExecution(
                     createHandler(), "art", "on-ota-staged", "--slot", "_b")) {
            assertThat(execution.getStdout().readLine()).contains("Job running...");

            try (var execution2 = new CommandExecution(
                         createHandler(), "art", "pr-dexopt-job", "--cancel")) {
                int exitCode2 = execution2.waitAndGetExitCode();
                String outputs2 = getOutputs(execution2);
                assertWithMessage(outputs2).that(exitCode2).isEqualTo(0);
                assertThat(outputs2).contains("Pre-reboot Dexopt job cancelled");
            }

            int exitCode = execution.waitAndGetExitCode();
            String outputs = getOutputs(execution);
            assertWithMessage(outputs).that(exitCode).isEqualTo(0);
            assertThat(outputs).contains("Job finished. See logs for details");
        }
    }

    @Test
    public void testOnOtaStagedSyncCancelledByBrokenPipe() throws Exception {
        when(mPreRebootDexoptJobInjector.isAtLeastB()).thenReturn(false);
        when(mInjector.getCallingUid()).thenReturn(Process.ROOT_UID);

        when(mPreRebootDriver.run(eq("_b"), eq(false) /* mapSnapshotsForOta */, any(), any()))
                .thenAnswer(invocation -> {
                    Semaphore dexoptCancelled = new Semaphore(0 /* permits */);
                    var cancellationSignal = invocation.<CancellationSignal>getArgument(2);
                    cancellationSignal.setOnCancelListener(() -> dexoptCancelled.release());
                    assertThat(dexoptCancelled.tryAcquire(TIMEOUT_SEC, TimeUnit.SECONDS)).isTrue();
                    return new PreRebootResult(Status.STATUS_FINISHED);
                });

        try (var execution = new CommandExecution(
                     createHandler(), "art", "on-ota-staged", "--slot", "_b")) {
            assertThat(execution.getStdout().readLine()).contains("Job running...");

            execution.closeStdin();

            int exitCode = execution.waitAndGetExitCode();
            String outputs = getOutputs(execution);
            assertWithMessage(outputs).that(exitCode).isEqualTo(0);
            assertThat(outputs).contains("Job finished. See logs for details");
        }
    }

    @Test
    @RequiresFlagsEnabled(FLAGS_PREFIX + Flags.FLAG_HYBRID_PRE_REBOOT_DEXOPT)
    public void testOnOtaStagedHybrid() throws Exception {
        when(mPreRebootDexoptJobInjector.isAtLeastB()).thenReturn(true);
        when(mInjector.getCallingUid()).thenReturn(Process.ROOT_UID);

        // Set the synchronous time limit to 3 minutes.
        when(SystemProperties.getInt(eq("dalvik.vm.pr_dexopt_sync_time_limit_millis"), anyInt()))
                .thenReturn(180000);

        // Simulate a long running synchronous job that has to be cancelled.
        Semaphore dexoptCancelled = new Semaphore(0);
        doAnswer(invocation -> {
            var cancellationSignal = invocation.<CancellationSignal>getArgument(2);
            cancellationSignal.setOnCancelListener(() -> dexoptCancelled.release());
            assertThat(dexoptCancelled.tryAcquire(TIMEOUT_SEC, TimeUnit.SECONDS)).isTrue();
            return new PreRebootResult(Status.STATUS_FINISHED);
        })
                .when(mPreRebootDriver)
                .run(any(), anyBoolean(), any(), any());

        // Synchronous job run.
        try (var execution = new CommandExecution(
                     createHandler(), "art", "on-ota-staged", "--slot", "_b")) {
            assertThat(execution.getStdout().readLine()).contains("Job running...");
            assertThat(execution.getStdout().readLine()).isEqualTo("global_progress 0.000000");

            verify(mPreRebootDriver)
                    .run(eq("_b"), eq(false), any(),
                            eq(ReasonMapping.REASON_PRE_REBOOT_DEXOPT_SYNC));

            var session =
                    mPreRebootStatsReporterHarness.createStatsReporter().new ProgressSession();

            // Simulate 5 skipped, 4 optimized, 1 failed, 100 total. Progress fraction is 10/100.
            session.recordProgress(5, 4, 1, 100, 4);

            // Simulate 1 second has passed. Time fraction is 1/180.
            mMockClock.waitForSleepers(1);
            mMockClock.advanceTime(1000);

            // The progress fraction is ahead of the time fraction and therefore controls.
            assertThat(execution.getStdout().readLine()).isEqualTo("global_progress 0.100000");

            // Simulate 5 skipped, 45 optimized, 1 failed, 100 total. Progress fraction is 51/100.
            session.recordProgress(5, 45, 1, 100, 45);

            // Simulate another 161 seconds have passed. Time fraction is 162/180.
            mMockClock.waitForSleepers(1);
            mMockClock.advanceTime(161000);

            // The time fraction is ahead of the progress fraction and therefore controls.
            assertThat(execution.getStdout().readLine()).isEqualTo("global_progress 0.900000");

            // Simulate another 18 seconds have passed. The timeout is triggered.
            mMockClock.advanceTime(18000);

            int exitCode = execution.waitAndGetExitCode();
            String outputs = getOutputs(execution);
            assertWithMessage(outputs).that(exitCode).isEqualTo(0);
            assertThat(outputs).endsWith("""
                    global_progress 1.000000
                    Job finished. See logs for details
                    Asynchronous Pre-reboot Dexopt job scheduled
                    """);
        }

        // Simulate a long running asynchronous job.
        doAnswer(invocation -> {
            mMockClock.sleep(600000);
            return new PreRebootResult(Status.STATUS_FINISHED);
        })
                .when(mPreRebootDriver)
                .run(any(), anyBoolean(), any(), any());

        mPreRebootDexoptJob.onStartJobImpl(mJobService, mJobParameters);

        // Asynchronous job run.
        try (var execution =
                        new CommandExecution(createHandler(), "art", "on-ota-staged", "--start")) {
            assertThat(execution.getStdout().readLine()).contains("Job running...");
            assertThat(execution.getStdout().readLine()).isEqualTo("global_progress 0.000000");

            verify(mPreRebootDriver)
                    .run(eq("_b"), eq(false), any(), eq(ReasonMapping.REASON_PRE_REBOOT_DEXOPT));

            var session =
                    mPreRebootStatsReporterHarness.createStatsReporter().new ProgressSession();

            // Simulate 5 skipped, 45 optimized, 1 failed, 100 total. Progress fraction is 51/100.
            session.recordProgress(5, 45, 1, 100, 45);

            // Simulate 180 seconds have passed.
            mMockClock.waitForSleepers(2);
            mMockClock.advanceTime(180000);

            // There's no time limit for async job, so the progress fraction always controls.
            assertThat(execution.getStdout().readLine()).isEqualTo("global_progress 0.510000");

            // Simulate async job finished.
            mMockClock.advanceTime(420000);

            int exitCode = execution.waitAndGetExitCode();
            String outputs = getOutputs(execution);
            assertWithMessage(outputs).that(exitCode).isEqualTo(0);
            assertThat(outputs).endsWith("""
                    global_progress 1.000000
                    Job finished. See logs for details
                    """);
        }
    }

    @Test
    @RequiresFlagsEnabled(FLAGS_PREFIX + Flags.FLAG_HYBRID_PRE_REBOOT_DEXOPT)
    public void testOnOtaStagedHybridSyncCompletesBeforeTimeout() throws Exception {
        when(mPreRebootDexoptJobInjector.isAtLeastB()).thenReturn(true);
        when(mInjector.getCallingUid()).thenReturn(Process.ROOT_UID);

        // Set the synchronous time limit to 3 minutes.
        when(SystemProperties.getInt(eq("dalvik.vm.pr_dexopt_sync_time_limit_millis"), anyInt()))
                .thenReturn(180000);

        // Simulate a synchronous job that completes before the timeout.
        doAnswer(invocation -> {
            mMockClock.sleep(120000);
            return new PreRebootResult(Status.STATUS_FINISHED);
        })
                .when(mPreRebootDriver)
                .run(any(), anyBoolean(), any(), any());

        // Synchronous job run.
        try (var execution = new CommandExecution(
                     createHandler(), "art", "on-ota-staged", "--slot", "_b")) {
            assertThat(execution.getStdout().readLine()).contains("Job running...");
            assertThat(execution.getStdout().readLine()).isEqualTo("global_progress 0.000000");

            mMockClock.waitForSleepers(1);
            mMockClock.advanceTime(120000);

            int exitCode = execution.waitAndGetExitCode();
            String outputs = getOutputs(execution);
            assertWithMessage(outputs).that(exitCode).isEqualTo(0);
            assertThat(outputs).endsWith("""
                    global_progress 1.000000
                    Job finished. See logs for details
                    """);
        }

        verify(mJobScheduler, never()).schedule(any());
    }

    @Test
    @RequiresFlagsDisabled(FLAGS_PREFIX + Flags.FLAG_HYBRID_PRE_REBOOT_DEXOPT)
    public void testOnOtaStagedAsync() throws Exception {
        when(mPreRebootDexoptJobInjector.isAtLeastB()).thenReturn(true);
        when(mInjector.getCallingUid()).thenReturn(Process.ROOT_UID);

        try (var execution = new CommandExecution(
                     createHandler(), "art", "on-ota-staged", "--slot", "_b")) {
            int exitCode = execution.waitAndGetExitCode();
            String outputs = getOutputs(execution);
            assertWithMessage(outputs).that(exitCode).isEqualTo(0);
            assertThat(outputs).contains("Pre-reboot Dexopt job scheduled");
        }

        verify(mPreRebootDriver, never()).run(any(), anyBoolean(), any(), any());

        when(mPreRebootDriver.run(eq("_b"), eq(false) /* mapSnapshotsForOta */, any(), any()))
                .thenReturn(new PreRebootResult(Status.STATUS_FINISHED));

        mPreRebootDexoptJob.onStartJobImpl(mJobService, mJobParameters);

        try (var execution =
                        new CommandExecution(createHandler(), "art", "on-ota-staged", "--start")) {
            int exitCode = execution.waitAndGetExitCode();
            String outputs = getOutputs(execution);
            assertWithMessage(outputs).that(exitCode).isEqualTo(0);
            assertThat(outputs).contains("Job finished. See logs for details");
        }
    }

    @Test
    public void testOnOtaStagedAsyncFatalError() throws Exception {
        // Set the synchronous time limit to 0 to force the job to be asynchronous.
        when(SystemProperties.getInt(eq("dalvik.vm.pr_dexopt_sync_time_limit_millis"), anyInt()))
                .thenReturn(0);

        when(mPreRebootDexoptJobInjector.isAtLeastB()).thenReturn(true);
        when(mInjector.getCallingUid()).thenReturn(Process.ROOT_UID);

        try (var execution = new CommandExecution(
                     createHandler(), "art", "on-ota-staged", "--slot", "_b")) {
            int exitCode = execution.waitAndGetExitCode();
            String outputs = getOutputs(execution);
            assertWithMessage(outputs).that(exitCode).isEqualTo(0);
            assertThat(outputs).contains("Pre-reboot Dexopt job scheduled");
        }

        when(mPreRebootDriver.run(eq("_b"), eq(false) /* mapSnapshotsForOta */, any(), any()))
                .thenThrow(RuntimeException.class);

        mPreRebootDexoptJob.onStartJobImpl(mJobService, mJobParameters);

        try (var execution =
                        new CommandExecution(createHandler(), "art", "on-ota-staged", "--start")) {
            int exitCode = execution.waitAndGetExitCode();
            String outputs = getOutputs(execution);
            assertWithMessage(outputs).that(exitCode).isEqualTo(0);
            assertThat(outputs).contains("Job encountered a fatal error");
        }
    }

    @Test
    public void testOnOtaStagedAsyncCancelledByCommand() throws Exception {
        // Set the synchronous time limit to 0 to force the job to be asynchronous.
        when(SystemProperties.getInt(eq("dalvik.vm.pr_dexopt_sync_time_limit_millis"), anyInt()))
                .thenReturn(0);

        when(mPreRebootDexoptJobInjector.isAtLeastB()).thenReturn(true);
        when(mInjector.getCallingUid()).thenReturn(Process.ROOT_UID);

        try (var execution = new CommandExecution(
                     createHandler(), "art", "on-ota-staged", "--slot", "_b")) {
            int exitCode = execution.waitAndGetExitCode();
            String outputs = getOutputs(execution);
            assertWithMessage(outputs).that(exitCode).isEqualTo(0);
            assertThat(outputs).contains("Pre-reboot Dexopt job scheduled");
        }

        Semaphore dexoptStarted = new Semaphore(0);

        when(mPreRebootDriver.run(eq("_b"), eq(false) /* mapSnapshotsForOta */, any(), any()))
                .thenAnswer(invocation -> {
                    // Step 2.
                    dexoptStarted.release();

                    Semaphore dexoptCancelled = new Semaphore(0);
                    var cancellationSignal = invocation.<CancellationSignal>getArgument(2);
                    cancellationSignal.setOnCancelListener(() -> dexoptCancelled.release());
                    assertThat(dexoptCancelled.tryAcquire(TIMEOUT_SEC, TimeUnit.SECONDS)).isTrue();

                    // Step 4.
                    return new PreRebootResult(Status.STATUS_FINISHED);
                });

        mPreRebootDexoptJob.onStartJobImpl(mJobService, mJobParameters);

        // Step 1.
        try (var execution =
                        new CommandExecution(createHandler(), "art", "on-ota-staged", "--start")) {
            assertThat(execution.getStdout().readLine()).contains("Job running...");

            assertThat(dexoptStarted.tryAcquire(TIMEOUT_SEC, TimeUnit.SECONDS)).isTrue();

            // Step 3.
            try (var execution2 = new CommandExecution(
                         createHandler(), "art", "pr-dexopt-job", "--cancel")) {
                int exitCode2 = execution2.waitAndGetExitCode();
                String outputs2 = getOutputs(execution2);
                assertWithMessage(outputs2).that(exitCode2).isEqualTo(0);
                assertThat(outputs2).contains("Pre-reboot Dexopt job cancelled");
            }

            int exitCode = execution.waitAndGetExitCode();

            // Step 5.
            String outputs = getOutputs(execution);
            assertWithMessage(outputs).that(exitCode).isEqualTo(0);
            assertThat(outputs).contains("Job finished. See logs for details");
        }
    }

    @Test
    public void testOnOtaStagedAsyncCancelledByBrokenPipe() throws Exception {
        // Set the synchronous time limit to 0 to force the job to be asynchronous.
        when(SystemProperties.getInt(eq("dalvik.vm.pr_dexopt_sync_time_limit_millis"), anyInt()))
                .thenReturn(0);

        when(mPreRebootDexoptJobInjector.isAtLeastB()).thenReturn(true);
        when(mInjector.getCallingUid()).thenReturn(Process.ROOT_UID);

        try (var execution = new CommandExecution(
                     createHandler(), "art", "on-ota-staged", "--slot", "_b")) {
            int exitCode = execution.waitAndGetExitCode();
            String outputs = getOutputs(execution);
            assertWithMessage(outputs).that(exitCode).isEqualTo(0);
            assertThat(outputs).contains("Pre-reboot Dexopt job scheduled");
        }

        Semaphore dexoptStarted = new Semaphore(0);

        when(mPreRebootDriver.run(eq("_b"), eq(false) /* mapSnapshotsForOta */, any(), any()))
                .thenAnswer(invocation -> {
                    // Step 2.
                    dexoptStarted.release();

                    Semaphore dexoptCancelled = new Semaphore(0);
                    var cancellationSignal = invocation.<CancellationSignal>getArgument(2);
                    cancellationSignal.setOnCancelListener(() -> dexoptCancelled.release());
                    assertThat(dexoptCancelled.tryAcquire(TIMEOUT_SEC, TimeUnit.SECONDS)).isTrue();

                    // Step 4.
                    return new PreRebootResult(Status.STATUS_FINISHED);
                });

        mPreRebootDexoptJob.onStartJobImpl(mJobService, mJobParameters);

        // Step 1.
        try (var execution =
                        new CommandExecution(createHandler(), "art", "on-ota-staged", "--start")) {
            assertThat(execution.getStdout().readLine()).contains("Job running...");

            assertThat(dexoptStarted.tryAcquire(TIMEOUT_SEC, TimeUnit.SECONDS)).isTrue();

            // Step 3.
            execution.closeStdin();

            int exitCode = execution.waitAndGetExitCode();

            // Step 5.
            String outputs = getOutputs(execution);
            assertWithMessage(outputs).that(exitCode).isEqualTo(0);
            assertThat(outputs).contains("Job finished. See logs for details");
        }
    }

    @Test
    public void testOnOtaStagedAsyncCancelledByJobScheduler() throws Exception {
        // Set the synchronous time limit to 0 to force the job to be asynchronous.
        when(SystemProperties.getInt(eq("dalvik.vm.pr_dexopt_sync_time_limit_millis"), anyInt()))
                .thenReturn(0);

        when(mPreRebootDexoptJobInjector.isAtLeastB()).thenReturn(true);
        when(mInjector.getCallingUid()).thenReturn(Process.ROOT_UID);

        try (var execution = new CommandExecution(
                     createHandler(), "art", "on-ota-staged", "--slot", "_b")) {
            int exitCode = execution.waitAndGetExitCode();
            String outputs = getOutputs(execution);
            assertWithMessage(outputs).that(exitCode).isEqualTo(0);
            assertThat(outputs).contains("Pre-reboot Dexopt job scheduled");
        }

        mPreRebootDexoptJob.onStartJobImpl(mJobService, mJobParameters);
        mPreRebootDexoptJob.onStopJobImpl(mJobParameters);

        mPreRebootDexoptJob.waitForRunningJob();
        verify(mUpdateEngine).triggerPostinstall("system");
        verify(mPreRebootDriver, never()).run(any(), anyBoolean(), any(), any());
    }

    @Test
    public void testOnOtaStagedAsyncLegacy() throws Exception {
        // Set the synchronous time limit to 0 to force the job to be asynchronous.
        when(SystemProperties.getInt(eq("dalvik.vm.pr_dexopt_sync_time_limit_millis"), anyInt()))
                .thenReturn(0);

        when(mPreRebootDexoptJobInjector.isAtLeastB()).thenReturn(false);
        when(mInjector.getCallingUid()).thenReturn(Process.ROOT_UID);

        when(SystemProperties.getBoolean(eq("dalvik.vm.pr_dexopt_async_for_ota"), anyBoolean()))
                .thenReturn(true);

        try (var execution = new CommandExecution(
                     createHandler(), "art", "on-ota-staged", "--slot", "_b")) {
            int exitCode = execution.waitAndGetExitCode();
            String outputs = getOutputs(execution);
            assertWithMessage(outputs).that(exitCode).isEqualTo(0);
            assertThat(outputs).contains("Pre-reboot Dexopt job scheduled");
        }

        when(mPreRebootDriver.run(eq("_b"), eq(true) /* mapSnapshotsForOta */, any(),
                     eq(ReasonMapping.REASON_PRE_REBOOT_DEXOPT)))
                .thenReturn(new PreRebootResult(Status.STATUS_FINISHED));

        mPreRebootDexoptJob.onStartJobImpl(mJobService, mJobParameters);
        mPreRebootDexoptJob.waitForRunningJob();
    }

    @Test
    public void testOnOtaStagedStartJobNotFound() throws Exception {
        when(mInjector.getCallingUid()).thenReturn(Process.ROOT_UID);

        try (var execution =
                        new CommandExecution(createHandler(), "art", "on-ota-staged", "--start")) {
            int exitCode = execution.waitAndGetExitCode();
            String outputs = getOutputs(execution);
            assertWithMessage(outputs).that(exitCode).isEqualTo(1);
            assertThat(outputs).contains("No waiting job found");
        }
    }

    @Test
    public void testPrDexoptJobRunMainline() throws Exception {
        when(mInjector.getCallingUid()).thenReturn(Process.SHELL_UID);

        when(mPreRebootDriver.run(isNull() /* otaSlot */, anyBoolean() /* mapSnapshotsForOta */,
                     any(), eq(ReasonMapping.REASON_PRE_REBOOT_DEXOPT)))
                .thenReturn(new PreRebootResult(Status.STATUS_FINISHED));

        try (var execution =
                        new CommandExecution(createHandler(), "art", "pr-dexopt-job", "--run")) {
            int exitCode = execution.waitAndGetExitCode();
            String outputs = getOutputs(execution);
            assertWithMessage(outputs).that(exitCode).isEqualTo(0);
            assertThat(outputs).contains("Job finished. See logs for details");
        }
    }

    @Test
    public void testPrDexoptJobRunOtaPermission() throws Exception {
        when(mInjector.getCallingUid()).thenReturn(Process.SHELL_UID);

        try (var execution = new CommandExecution(
                     createHandler(), "art", "pr-dexopt-job", "--run", "--slot", "_b")) {
            int exitCode = execution.waitAndGetExitCode();
            String outputs = getOutputs(execution);
            assertWithMessage(outputs).that(exitCode).isEqualTo(-1);
            assertThat(outputs).contains("Only root can specify '--slot'");
        }
    }

    @Test
    public void testPrDexoptJobRunOtaLegacy() throws Exception {
        when(mPreRebootDexoptJobInjector.isAtLeastB()).thenReturn(false);
        when(mInjector.getCallingUid()).thenReturn(Process.ROOT_UID);

        when(mPreRebootDriver.run(eq("_b"), eq(true) /* mapSnapshotsForOta */, any(),
                     eq(ReasonMapping.REASON_PRE_REBOOT_DEXOPT)))
                .thenReturn(new PreRebootResult(Status.STATUS_FINISHED));

        try (var execution = new CommandExecution(
                     createHandler(), "art", "pr-dexopt-job", "--run", "--slot", "_b")) {
            int exitCode = execution.waitAndGetExitCode();
            String outputs = getOutputs(execution);
            assertWithMessage(outputs).that(exitCode).isEqualTo(0);
            assertThat(outputs).contains("Job finished. See logs for details");
        }
    }

    @Test
    public void testPrDexoptJobRunOta() throws Exception {
        lenient().when(mPreRebootDexoptJobInjector.isAtLeastB()).thenReturn(true);
        when(mInjector.getCallingUid()).thenReturn(Process.ROOT_UID);

        when(mPreRebootDriver.run(eq("_b"), eq(false) /* mapSnapshotsForOta */, any(),
                     eq(ReasonMapping.REASON_PRE_REBOOT_DEXOPT)))
                .thenReturn(new PreRebootResult(Status.STATUS_FINISHED));

        try (var execution = new CommandExecution(
                     createHandler(), "art", "pr-dexopt-job", "--run", "--slot", "_b")) {
            assertThat(execution.getStdout().readLine()).contains("Job running...");

            try (var execution2 = new CommandExecution(
                         createHandler(), "art", "on-ota-staged", "--start")) {
                int exitCode2 = execution2.waitAndGetExitCode();
                String outputs2 = getOutputs(execution2);
                assertWithMessage(outputs2).that(exitCode2).isEqualTo(0);
                assertThat(outputs2).contains("Job finished. See logs for details");
            }

            int exitCode = execution.waitAndGetExitCode();
            String outputs = getOutputs(execution);
            assertWithMessage(outputs).that(exitCode).isEqualTo(0);
            assertThat(outputs).contains("Job finished. See logs for details");
        }
    }

    @Test
    public void testPrDexoptJobScheduleMainline() throws Exception {
        when(mInjector.getCallingUid()).thenReturn(Process.SHELL_UID);

        try (var execution = new CommandExecution(
                     createHandler(), "art", "pr-dexopt-job", "--schedule")) {
            int exitCode = execution.waitAndGetExitCode();
            String outputs = getOutputs(execution);
            assertWithMessage(outputs).that(exitCode).isEqualTo(0);
            assertThat(outputs).contains("Pre-reboot Dexopt job scheduled");
        }

        when(mPreRebootDriver.run(isNull() /* otaSlot */, anyBoolean() /* mapSnapshotsForOta */,
                     any(), eq(ReasonMapping.REASON_PRE_REBOOT_DEXOPT)))
                .thenReturn(new PreRebootResult(Status.STATUS_FINISHED));

        mPreRebootDexoptJob.onStartJobImpl(mJobService, mJobParameters);
        mPreRebootDexoptJob.waitForRunningJob();
    }

    @Test
    public void testPrDexoptJobScheduleOtaPermission() throws Exception {
        when(mInjector.getCallingUid()).thenReturn(Process.SHELL_UID);

        try (var execution = new CommandExecution(
                     createHandler(), "art", "pr-dexopt-job", "--schedule", "--slot", "_b")) {
            int exitCode = execution.waitAndGetExitCode();
            String outputs = getOutputs(execution);
            assertWithMessage(outputs).that(exitCode).isEqualTo(-1);
            assertThat(outputs).contains("Only root can specify '--slot'");
        }
    }

    @Test
    public void testPrDexoptJobScheduleOtaLegacy() throws Exception {
        when(mPreRebootDexoptJobInjector.isAtLeastB()).thenReturn(false);
        when(mInjector.getCallingUid()).thenReturn(Process.ROOT_UID);

        try (var execution = new CommandExecution(
                     createHandler(), "art", "pr-dexopt-job", "--schedule", "--slot", "_b")) {
            int exitCode = execution.waitAndGetExitCode();
            String outputs = getOutputs(execution);
            assertWithMessage(outputs).that(exitCode).isEqualTo(0);
            assertThat(outputs).contains("Pre-reboot Dexopt job scheduled");
        }

        when(mPreRebootDriver.run(eq("_b"), eq(true) /* mapSnapshotsForOta */, any(),
                     eq(ReasonMapping.REASON_PRE_REBOOT_DEXOPT)))
                .thenReturn(new PreRebootResult(Status.STATUS_FINISHED));

        mPreRebootDexoptJob.onStartJobImpl(mJobService, mJobParameters);
        mPreRebootDexoptJob.waitForRunningJob();
    }

    @Test
    public void testPrDexoptJobScheduleOta() throws Exception {
        when(mPreRebootDexoptJobInjector.isAtLeastB()).thenReturn(true);
        when(mInjector.getCallingUid()).thenReturn(Process.ROOT_UID);

        try (var execution = new CommandExecution(
                     createHandler(), "art", "pr-dexopt-job", "--schedule", "--slot", "_b")) {
            int exitCode = execution.waitAndGetExitCode();
            String outputs = getOutputs(execution);
            assertWithMessage(outputs).that(exitCode).isEqualTo(0);
            assertThat(outputs).contains("Pre-reboot Dexopt job scheduled");
        }

        when(mPreRebootDriver.run(eq("_b"), eq(false) /* mapSnapshotsForOta */, any(),
                     eq(ReasonMapping.REASON_PRE_REBOOT_DEXOPT)))
                .thenReturn(new PreRebootResult(Status.STATUS_FINISHED));

        mPreRebootDexoptJob.onStartJobImpl(mJobService, mJobParameters);

        try (var execution =
                        new CommandExecution(createHandler(), "art", "on-ota-staged", "--start")) {
            int exitCode = execution.waitAndGetExitCode();
            String outputs = getOutputs(execution);
            assertWithMessage(outputs).that(exitCode).isEqualTo(0);
            assertThat(outputs).contains("Job finished. See logs for details");
        }

        mPreRebootDexoptJob.waitForRunningJob();
    }

    @Test
    @RequiresFlagsEnabled(FLAGS_PREFIX + Flags.FLAG_HYBRID_PRE_REBOOT_DEXOPT)
    public void testPrDexoptJobHybridOta() throws Exception {
        when(mPreRebootDexoptJobInjector.isAtLeastB()).thenReturn(true);
        when(mInjector.getCallingUid()).thenReturn(Process.ROOT_UID);

        // Set the synchronous time limit to 3 minutes.
        when(SystemProperties.getInt(eq("dalvik.vm.pr_dexopt_sync_time_limit_millis"), anyInt()))
                .thenReturn(180000);

        // Simulate a long running synchronous job that has to be cancelled.
        Semaphore dexoptCancelled = new Semaphore(0);
        doAnswer(invocation -> {
            var cancellationSignal = invocation.<CancellationSignal>getArgument(2);
            cancellationSignal.setOnCancelListener(() -> dexoptCancelled.release());
            assertThat(dexoptCancelled.tryAcquire(TIMEOUT_SEC, TimeUnit.SECONDS)).isTrue();
            return new PreRebootResult(Status.STATUS_FINISHED);
        })
                .when(mPreRebootDriver)
                .run(any(), anyBoolean(), any(), any());

        // Synchronous job run.
        try (var execution = new CommandExecution(
                     createHandler(), "art", "pr-dexopt-job", "--hybrid", "--slot", "_b")) {
            assertThat(execution.getStdout().readLine()).contains("Job running...");
            assertThat(execution.getStdout().readLine()).isEqualTo("Progress: 0.00%");

            try (var execution2 = new CommandExecution(
                         createHandler(), "art", "on-ota-staged", "--start")) {
                assertThat(execution2.getStdout().readLine()).contains("Job running...");
                assertThat(execution2.getStdout().readLine()).isEqualTo("global_progress 0.000000");

                verify(mPreRebootDriver)
                        .run(eq("_b"), eq(false), any(),
                                eq(ReasonMapping.REASON_PRE_REBOOT_DEXOPT_SYNC));

                var session =
                        mPreRebootStatsReporterHarness.createStatsReporter().new ProgressSession();

                // Simulate 5 skipped, 4 optimized, 1 failed, 100 total. Progress fraction is
                // 10/100.
                session.recordProgress(5, 4, 1, 100, 4);

                // Simulate 1 second has passed. Time fraction is 1/180.
                mMockClock.waitForSleepers(2);
                mMockClock.advanceTime(1000);

                // The progress fraction is ahead of the time fraction and therefore controls.
                assertThat(execution.getStdout().readLine()).isEqualTo("Progress: 10.00%");
                assertThat(execution2.getStdout().readLine()).isEqualTo("global_progress 0.100000");

                // Simulate 5 skipped, 45 optimized, 1 failed, 100 total. Progress fraction is
                // 51/100.
                session.recordProgress(5, 45, 1, 100, 45);

                // Simulate another 161 seconds have passed. Time fraction is 162/180.
                mMockClock.waitForSleepers(2);
                mMockClock.advanceTime(161000);

                // The time fraction is ahead of the progress fraction and therefore controls.
                assertThat(execution.getStdout().readLine()).isEqualTo("Progress: 90.00%");
                assertThat(execution2.getStdout().readLine()).isEqualTo("global_progress 0.900000");

                // Simulate another 18 seconds have passed. The timeout is triggered.
                mMockClock.advanceTime(18000);

                int exitCode2 = execution2.waitAndGetExitCode();
                String outputs2 = getOutputs(execution2);
                assertWithMessage(outputs2).that(exitCode2).isEqualTo(0);
                assertThat(outputs2).endsWith("""
                        global_progress 1.000000
                        Job finished. See logs for details
                        """);
            }

            int exitCode = execution.waitAndGetExitCode();
            String outputs = getOutputs(execution);
            assertWithMessage(outputs).that(exitCode).isEqualTo(0);
            assertThat(outputs).endsWith("""
                    Progress: 100.00%
                    Job finished. See logs for details
                    Asynchronous Pre-reboot Dexopt job scheduled
                    """);
        }

        // Simulate a long running asynchronous job.
        doAnswer(invocation -> {
            mMockClock.sleep(600000);
            return new PreRebootResult(Status.STATUS_FINISHED);
        })
                .when(mPreRebootDriver)
                .run(any(), anyBoolean(), any(), any());

        mPreRebootDexoptJob.onStartJobImpl(mJobService, mJobParameters);

        // Asynchronous job run.
        try (var execution =
                        new CommandExecution(createHandler(), "art", "on-ota-staged", "--start")) {
            assertThat(execution.getStdout().readLine()).contains("Job running...");
            assertThat(execution.getStdout().readLine()).isEqualTo("global_progress 0.000000");

            verify(mPreRebootDriver)
                    .run(eq("_b"), eq(false), any(), eq(ReasonMapping.REASON_PRE_REBOOT_DEXOPT));

            var session =
                    mPreRebootStatsReporterHarness.createStatsReporter().new ProgressSession();

            // Simulate 5 skipped, 45 optimized, 1 failed, 100 total. Progress fraction is 51/100.
            session.recordProgress(5, 45, 1, 100, 45);

            // Simulate 180 seconds have passed.
            mMockClock.waitForSleepers(2);
            mMockClock.advanceTime(180000);

            // There's no time limit for async job, so the progress fraction always controls.
            assertThat(execution.getStdout().readLine()).isEqualTo("global_progress 0.510000");

            // Simulate async job finished.
            mMockClock.advanceTime(420000);

            int exitCode = execution.waitAndGetExitCode();
            String outputs = getOutputs(execution);
            assertWithMessage(outputs).that(exitCode).isEqualTo(0);
            assertThat(outputs).endsWith("""
                    global_progress 1.000000
                    Job finished. See logs for details
                    """);
        }
    }

    @Test
    public void testPrDexoptJobCancelJobNotFound() throws Exception {
        when(mInjector.getCallingUid()).thenReturn(Process.ROOT_UID);

        try (var execution =
                        new CommandExecution(createHandler(), "art", "pr-dexopt-job", "--cancel")) {
            int exitCode = execution.waitAndGetExitCode();
            String outputs = getOutputs(execution);
            assertWithMessage(outputs).that(exitCode).isEqualTo(0);
            assertThat(outputs).contains("Pre-reboot Dexopt job cancelled");
        }
    }

    @Test
    public void testVerifyDexoptArtifacts() throws Exception {
        when(mInjector.getCallingUid()).thenReturn(Process.ROOT_UID);
        when(mDexoptHelper.verifyDexoptArtifacts(any(), any()))
                .thenReturn(new VerifyDexoptArtifactsResult(true));

        try (var execution =
                        new CommandExecution(createHandler(), "art", "verify-dexopt-artifacts")) {
            int exitCode = execution.waitAndGetExitCode();
            String outputs = getOutputs(execution);
            assertThat(exitCode).isEqualTo(0);
            assertThat(outputs).contains("Verifying dexopt artifacts...");
            assertThat(outputs).contains("All dexopt artifacts are verified");
        }
    }

    @Test
    public void testVerifyDexoptArtifactsFailure() throws Exception {
        when(mInjector.getCallingUid()).thenReturn(Process.ROOT_UID);
        when(mDexoptHelper.verifyDexoptArtifacts(any(), any()))
                .thenReturn(new VerifyDexoptArtifactsResult(false));

        try (var execution =
                        new CommandExecution(createHandler(), "art", "verify-dexopt-artifacts")) {
            int exitCode = execution.waitAndGetExitCode();
            String outputs = getOutputs(execution);
            assertThat(exitCode).isEqualTo(1);
            assertThat(outputs).contains("Verifying dexopt artifacts...");
            assertThat(outputs).contains("Verification failed");
        }
    }

    @Test
    public void testVerifyDexoptArtifactsUnsupported() throws Exception {
        when(mInjector.getCallingUid()).thenReturn(Process.ROOT_UID);
        when(mInjector.isVerificationSupported()).thenReturn(false);

        try (var execution =
                        new CommandExecution(createHandler(), "art", "verify-dexopt-artifacts")) {
            int exitCode = execution.waitAndGetExitCode();
            String outputs = getOutputs(execution);
            assertThat(exitCode).isEqualTo(1);
            assertThat(outputs).contains("Error: Unsupported command 'verify-dexopt-artifacts'");
        }
    }

    private ArtShellCommand createHandler() {
        return new ArtShellCommand(mInjector);
    }

    private String getOutputs(CommandExecution execution) {
        return Stream.concat(execution.getStdout().lines(), execution.getStderr().lines())
                .collect(Collectors.joining(
                        "\n" /* delimiter */, "" /* prefix */, "\n" /* suffix */));
    }
}
