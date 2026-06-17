/*
 * Copyright (C) 2024 The Android Open Source Project
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

import static com.android.server.art.PreRebootDexoptJob.JOB_ID;

import static com.google.common.truth.Truth.assertThat;

import static org.mockito.Mockito.any;
import static org.mockito.Mockito.anyBoolean;
import static org.mockito.Mockito.anyInt;
import static org.mockito.Mockito.atLeast;
import static org.mockito.Mockito.doAnswer;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.doThrow;
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
import android.os.ServiceSpecificException;
import android.os.SystemProperties;
import android.os.UpdateEngine;
import android.provider.DeviceConfig;

import androidx.test.filters.SmallTest;

import com.android.server.art.PreRebootDexoptJob.JobSynchronicity;
import com.android.server.art.PreRebootDexoptJob.OnUpdateReadyResponse;
import com.android.server.art.PreRebootDexoptJob.StagedFilesAge;
import com.android.server.art.model.ArtFlags;
import com.android.server.art.prereboot.PreRebootDriver;
import com.android.server.art.prereboot.PreRebootDriver.PreRebootResult;
import com.android.server.art.prereboot.PreRebootStatsReporter;
import com.android.server.art.proto.PreRebootStats.Status;
import com.android.server.art.testing.MockClock;
import com.android.server.art.testing.PreRebootStatsReporterHarness;
import com.android.server.art.testing.StaticMockitoRule;
import com.android.server.art.testing.TestingUtils;
import com.android.server.art.utils.Utils;

import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

import java.time.Duration;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.Semaphore;
import java.util.concurrent.TimeUnit;
import java.util.function.Supplier;

@SmallTest
@RunWith(MockitoJUnitRunner.StrictStubs.class)
public class PreRebootDexoptJobTest {
    private static final long TIMEOUT_SEC = 10;

    @Rule
    public StaticMockitoRule mockitoRule =
            new StaticMockitoRule(SystemProperties.class, BackgroundDexoptJobService.class);

    @Mock private PreRebootDexoptJob.Injector mInjector;
    @Mock private JobScheduler mJobScheduler;
    @Mock private PreRebootDriver mPreRebootDriver;
    @Mock private BackgroundDexoptJobService mJobService;
    @Mock private IArtd mArtd;
    @Mock private UpdateEngine mUpdateEngine;
    private PreRebootDexoptJob mPreRebootDexoptJob;
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

        // By default, the job is enabled by a build-time flag.
        lenient()
                .when(SystemProperties.getBoolean(eq("pm.dexopt.disable_bg_dexopt"), anyBoolean()))
                .thenReturn(false);
        lenient()
                .when(SystemProperties.getBoolean(eq("dalvik.vm.enable_pr_dexopt"), anyBoolean()))
                .thenReturn(true);
        lenient()
                .when(mInjector.getDeviceConfigBoolean(
                        eq(DeviceConfig.NAMESPACE_RUNTIME), eq("enable_pr_dexopt"), anyBoolean()))
                .thenReturn(false);

        lenient().when(mInjector.getJobScheduler()).thenReturn(mJobScheduler);
        lenient().when(mInjector.getPreRebootDriver()).thenReturn(mPreRebootDriver);
        lenient()
                .when(mInjector.getStatsReporter())
                .thenReturn(mPreRebootStatsReporterHarness.createStatsReporter());
        lenient().when(mInjector.getArtd()).thenReturn(mArtd);
        lenient().when(mInjector.getUpdateEngine()).thenReturn(mUpdateEngine);
        lenient().when(mInjector.getClock()).thenReturn(mMockClock);
        lenient().when(mInjector.getAsyncExecutor()).thenReturn(mMockClock.getAsyncExecutor());
        lenient().when(mInjector.isAtLeastB()).thenReturn(true);

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

        mPreRebootDexoptJob = new PreRebootDexoptJob(mInjector);
        lenient().when(BackgroundDexoptJobService.getJob(JOB_ID)).thenReturn(mPreRebootDexoptJob);

        lenient()
                .doAnswer(invocation -> {
                    CompletableFuture<?> unused = mPreRebootDexoptJob.notifyUpdateEngineReady();
                    return null;
                })
                .when(mUpdateEngine)
                .triggerPostinstall("system");
    }

    @Test
    public void testSchedule() throws Exception {
        OnUpdateReadyResponse response = Utils.getFuture(mPreRebootDexoptJob.onUpdateReady(
                null /* otaSlot */, false /* isUpdateEngineReady */, JobSynchronicity.ASYNC));
        assertThat(Utils.getFuture(response.asynchronousJobScheduling()))
                .isEqualTo(ArtFlags.SCHEDULE_SUCCESS);

        assertThat(mJobInfo.isPeriodic()).isFalse();
        assertThat(mJobInfo.isRequireDeviceIdle()).isTrue();
        assertThat(mJobInfo.isRequireCharging()).isTrue();
        assertThat(mJobInfo.isRequireBatteryNotLow()).isTrue();

        mPreRebootStatsReporterHarness.recordFakeAfterRebootDataAndReport();
        mPreRebootStatsReporterHarness.verifyJobStats(Status.STATUS_SCHEDULED);
    }

    @Test
    public void testScheduleDisabled() {
        when(SystemProperties.getBoolean(eq("pm.dexopt.disable_bg_dexopt"), anyBoolean()))
                .thenReturn(true);

        OnUpdateReadyResponse response = Utils.getFuture(mPreRebootDexoptJob.onUpdateReady(
                null /* otaSlot */, false /* isUpdateEngineReady */, JobSynchronicity.ASYNC));
        assertThat(response.asynchronousJobScheduling()).isNull();

        verify(mJobScheduler, never()).schedule(any());

        mPreRebootStatsReporterHarness.recordFakeAfterRebootDataAndReport();
        mPreRebootStatsReporterHarness.verifyJobStats(Status.STATUS_NOT_SCHEDULED_DISABLED);
    }

    @Test
    public void testSyncStartDisabled() throws Exception {
        when(SystemProperties.getBoolean(eq("pm.dexopt.disable_bg_dexopt"), anyBoolean()))
                .thenReturn(true);

        OnUpdateReadyResponse response = Utils.getFuture(mPreRebootDexoptJob.onUpdateReady(
                null /* otaSlot */, true /* isUpdateEngineReady */, JobSynchronicity.SYNC));

        assertThat(response.synchronousJob()).isNull();
        verify(mPreRebootDriver, never()).run(any(), anyBoolean(), any(), any());

        mPreRebootStatsReporterHarness.recordFakeAfterRebootDataAndReport();
        mPreRebootStatsReporterHarness.verifyJobStats(Status.STATUS_NOT_SCHEDULED_DISABLED);
    }

    @Test
    public void testScheduleNotEnabled() {
        when(SystemProperties.getBoolean(eq("dalvik.vm.enable_pr_dexopt"), anyBoolean()))
                .thenReturn(false);

        OnUpdateReadyResponse response = Utils.getFuture(mPreRebootDexoptJob.onUpdateReady(
                null /* otaSlot */, false /* isUpdateEngineReady */, JobSynchronicity.ASYNC));
        assertThat(response.asynchronousJobScheduling()).isNull();

        verify(mJobScheduler, never()).schedule(any());

        mPreRebootStatsReporterHarness.recordFakeAfterRebootDataAndReport();
        mPreRebootStatsReporterHarness.verifyJobStats(Status.STATUS_NOT_SCHEDULED_DISABLED);
    }

    @Test
    public void testSyncStartNotEnabled() throws Exception {
        when(SystemProperties.getBoolean(eq("dalvik.vm.enable_pr_dexopt"), anyBoolean()))
                .thenReturn(false);

        OnUpdateReadyResponse response = Utils.getFuture(mPreRebootDexoptJob.onUpdateReady(
                null /* otaSlot */, true /* isUpdateEngineReady */, JobSynchronicity.SYNC));

        assertThat(response.synchronousJob()).isNull();
        verify(mPreRebootDriver, never()).run(any(), anyBoolean(), any(), any());

        mPreRebootStatsReporterHarness.recordFakeAfterRebootDataAndReport();
        mPreRebootStatsReporterHarness.verifyJobStats(Status.STATUS_NOT_SCHEDULED_DISABLED);
    }

    @Test
    public void testScheduleEnabledByPhenotypeFlag() {
        lenient()
                .when(SystemProperties.getBoolean(eq("dalvik.vm.enable_pr_dexopt"), anyBoolean()))
                .thenReturn(false);
        lenient()
                .when(mInjector.getDeviceConfigBoolean(
                        eq(DeviceConfig.NAMESPACE_RUNTIME), eq("enable_pr_dexopt"), anyBoolean()))
                .thenReturn(true);

        OnUpdateReadyResponse response = Utils.getFuture(mPreRebootDexoptJob.onUpdateReady(
                null /* otaSlot */, false /* isUpdateEngineReady */, JobSynchronicity.ASYNC));
        assertThat(Utils.getFuture(response.asynchronousJobScheduling()))
                .isEqualTo(ArtFlags.SCHEDULE_SUCCESS);

        verify(mJobScheduler).schedule(any());

        mPreRebootStatsReporterHarness.recordFakeAfterRebootDataAndReport();
        mPreRebootStatsReporterHarness.verifyJobStats(Status.STATUS_SCHEDULED);
    }

    @Test
    public void testScheduleForceDisabledByPhenotypeFlag() {
        lenient()
                .when(SystemProperties.getBoolean(eq("dalvik.vm.enable_pr_dexopt"), anyBoolean()))
                .thenReturn(true);
        lenient()
                .when(mInjector.getDeviceConfigBoolean(
                        eq(DeviceConfig.NAMESPACE_RUNTIME), eq("enable_pr_dexopt"), anyBoolean()))
                .thenReturn(true);
        when(mInjector.getDeviceConfigBoolean(eq(DeviceConfig.NAMESPACE_RUNTIME),
                     eq("force_disable_pr_dexopt"), anyBoolean()))
                .thenReturn(true);

        OnUpdateReadyResponse response = Utils.getFuture(mPreRebootDexoptJob.onUpdateReady(
                null /* otaSlot */, false /* isUpdateEngineReady */, JobSynchronicity.ASYNC));
        assertThat(response.asynchronousJobScheduling()).isNull();

        verify(mJobScheduler, never()).schedule(any());

        mPreRebootStatsReporterHarness.recordFakeAfterRebootDataAndReport();
        mPreRebootStatsReporterHarness.verifyJobStats(Status.STATUS_NOT_SCHEDULED_DISABLED);
    }

    @Test
    public void testUnschedule() {
        OnUpdateReadyResponse response = Utils.getFuture(mPreRebootDexoptJob.onUpdateReady(
                null /* otaSlot */, false /* isUpdateEngineReady */, JobSynchronicity.ASYNC));
        Utils.getFuture(response.asynchronousJobScheduling());
        verify(mJobScheduler, atLeast(1)).cancel(JOB_ID);
    }

    private void checkStartAsync(String otaSlot, Supplier<Boolean> mapSnapshotsForOtaMatcher)
            throws Exception {
        var jobStarted = new Semaphore(0);
        when(mPreRebootDriver.run(eq(otaSlot), mapSnapshotsForOtaMatcher.get(), any(),
                     eq(ReasonMapping.REASON_PRE_REBOOT_DEXOPT)))
                .thenAnswer(invocation -> {
                    jobStarted.release();
                    return new PreRebootResult(Status.STATUS_FINISHED);
                });

        OnUpdateReadyResponse response = Utils.getFuture(mPreRebootDexoptJob.onUpdateReady(
                otaSlot, false /* isUpdateEngineReady */, JobSynchronicity.ASYNC));
        Utils.getFuture(response.asynchronousJobScheduling());
        mMockClock.advanceTime(123456);
        mPreRebootDexoptJob.onStartJobImpl(mJobService, mJobParameters);
        assertThat(jobStarted.tryAcquire(TIMEOUT_SEC, TimeUnit.SECONDS)).isTrue();

        mPreRebootDexoptJob.waitForRunningJob();

        mPreRebootStatsReporterHarness.recordFakeAfterRebootDataAndReport();
        mPreRebootStatsReporterHarness.verifyJobStats(Status.STATUS_FINISHED);
        mPreRebootStatsReporterHarness.verifyJobLatency(123456);
        mPreRebootStatsReporterHarness.verifySynchronicity(JobSynchronicity.ASYNC);
    }

    @Test
    public void testStartAsyncWithUpdateEngineApi() throws Exception {
        when(mInjector.isAtLeastB()).thenReturn(true);

        checkStartAsync("_b" /* otaSlot */, () -> eq(false) /* mapSnapshotsForOtaMatcher */);
        verify(mUpdateEngine).triggerPostinstall("system");
    }

    @Test
    public void testStartAsyncWithoutUpdateEngineApi() throws Exception {
        when(mInjector.isAtLeastB()).thenReturn(false);

        checkStartAsync("_b" /* otaSlot */, () -> eq(true) /* mapSnapshotsForOtaMatcher */);
        verify(mUpdateEngine, never()).triggerPostinstall(any());
    }

    @Test
    public void testStartAsyncMainline() throws Exception {
        checkStartAsync(null /* otaSlot */, () -> anyBoolean() /* mapSnapshotsForOtaMatcher */);
        verify(mUpdateEngine, never()).triggerPostinstall(any());
    }

    @Test
    public void testStartAsyncWithUpdateEngineApiSkippedDueToUpdateGone() throws Exception {
        when(mInjector.isAtLeastB()).thenReturn(true);

        final int POSTINTALL_RUNNER_ERROR = 5;
        doThrow(new ServiceSpecificException(POSTINTALL_RUNNER_ERROR,
                        "Postinstall action did not run. OTA update must first reach the "
                                + "Postinstall phase(which verfies that all partitions can be "
                                + "mounted) before calling TriggerPostinstall"))
                .when(mUpdateEngine)
                .triggerPostinstall("system");

        OnUpdateReadyResponse response = Utils.getFuture(mPreRebootDexoptJob.onUpdateReady(
                "_b", false /* isUpdateEngineReady */, JobSynchronicity.ASYNC));
        Utils.getFuture(response.asynchronousJobScheduling());
        mPreRebootDexoptJob.onStartJobImpl(mJobService, mJobParameters);

        mPreRebootDexoptJob.waitForRunningJob();

        mPreRebootStatsReporterHarness.recordFakeAfterRebootDataAndReport();
        mPreRebootStatsReporterHarness.verifyJobStats(Status.STATUS_FINISHED);
    }

    @Test
    public void testStartAsyncWithUpdateEngineApiFailedDueToUnknownError() throws Exception {
        when(mInjector.isAtLeastB()).thenReturn(true);

        final int POSTINTALL_RUNNER_ERROR = 5;
        doThrow(new ServiceSpecificException(POSTINTALL_RUNNER_ERROR, "Some unknown error"))
                .when(mUpdateEngine)
                .triggerPostinstall("system");

        OnUpdateReadyResponse response = Utils.getFuture(mPreRebootDexoptJob.onUpdateReady(
                "_b", false /* isUpdateEngineReady */, JobSynchronicity.ASYNC));
        Utils.getFuture(response.asynchronousJobScheduling());
        mPreRebootDexoptJob.onStartJobImpl(mJobService, mJobParameters);

        mPreRebootDexoptJob.waitForRunningJob();

        mPreRebootStatsReporterHarness.recordFakeAfterRebootDataAndReport();
        mPreRebootStatsReporterHarness.verifyJobStats(Status.STATUS_FAILED);
    }

    @Test
    @EnableFlags({android.os.Flags.FLAG_UPDATE_ENGINE_API})
    public void testStartWithUpdateEngineApiSkippedDueToUpdateGone() throws Exception {
        final int POSTINTALL_RUNNER_ERROR = 5;
        doThrow(new ServiceSpecificException(POSTINTALL_RUNNER_ERROR,
                        "Postinstall action did not run. OTA update must first reach the "
                                + "Postinstall phase(which verfies that all partitions can be "
                                + "mounted) before calling TriggerPostinstall"))
                .when(mUpdateEngine)
                .triggerPostinstall("system");

        mPreRebootDexoptJob.onUpdateReadyImpl("_b");
        mPreRebootDexoptJob.onStartJobImpl(mJobService, mJobParameters);

        mPreRebootDexoptJob.waitForRunningJob();

        mPreRebootStatsReporterHarness.recordFakeAfterRebootDataAndReport();
        mPreRebootStatsReporterHarness.verifyJobStats(Status.STATUS_FINISHED);
    }

    @Test
    @EnableFlags({android.os.Flags.FLAG_UPDATE_ENGINE_API})
    public void testStartWithUpdateEngineApiFailedDueToUnknownError() throws Exception {
        final int POSTINTALL_RUNNER_ERROR = 5;
        doThrow(new ServiceSpecificException(POSTINTALL_RUNNER_ERROR, "Some unknown error"))
                .when(mUpdateEngine)
                .triggerPostinstall("system");

        mPreRebootDexoptJob.onUpdateReadyImpl("_b");
        mPreRebootDexoptJob.onStartJobImpl(mJobService, mJobParameters);

        mPreRebootDexoptJob.waitForRunningJob();

        mPreRebootStatsReporterHarness.recordFakeAfterRebootDataAndReport();
        mPreRebootStatsReporterHarness.verifyJobStats(Status.STATUS_FAILED);
    }

    private void checkSyncStart(boolean isUpdateEngineReady, boolean expectedMapSnapshotsForOta)
            throws Exception {
        when(mPreRebootDriver.run(eq("_b"), eq(expectedMapSnapshotsForOta), any(),
                     eq(ReasonMapping.REASON_PRE_REBOOT_DEXOPT)))
                .thenReturn(new PreRebootResult(Status.STATUS_FINISHED));

        OnUpdateReadyResponse response = Utils.getFuture(mPreRebootDexoptJob.onUpdateReady(
                "_b" /* otaSlot */, isUpdateEngineReady, JobSynchronicity.SYNC));

        Utils.getFuture(response.synchronousJob());

        mPreRebootStatsReporterHarness.recordFakeAfterRebootDataAndReport();
        mPreRebootStatsReporterHarness.verifyJobStats(Status.STATUS_FINISHED);
        mPreRebootStatsReporterHarness.verifySynchronicity(JobSynchronicity.SYNC);
    }

    @Test
    public void testSyncStartWithUpdateEngineApi() throws Exception {
        when(mInjector.isAtLeastB()).thenReturn(true);

        checkSyncStart(false /* isUpdateEngineReady */, false /* expectedMapSnapshotsForOta */);
        verify(mUpdateEngine).triggerPostinstall("system");
    }

    @Test
    public void testSyncStartWithoutUpdateEngineApi() throws Exception {
        when(mInjector.isAtLeastB()).thenReturn(false);

        checkSyncStart(false /* isUpdateEngineReady */, true /* expectedMapSnapshotsForOta */);
        verify(mUpdateEngine, never()).triggerPostinstall(any());
    }

    @Test
    public void testSyncStartWithIsUpdateEngineReady() throws Exception {
        checkSyncStart(true /* isUpdateEngineReady */, false /* expectedMapSnapshotsForOta */);
        verify(mUpdateEngine, never()).triggerPostinstall(any());
    }

    @Test
    public void testCancel() {
        Semaphore dexoptCancelled = new Semaphore(0);
        Semaphore jobExited = new Semaphore(0);
        when(mPreRebootDriver.run(any(), anyBoolean(), any(), any())).thenAnswer(invocation -> {
            var cancellationSignal = invocation.<CancellationSignal>getArgument(2);
            cancellationSignal.setOnCancelListener(() -> dexoptCancelled.release());
            assertThat(dexoptCancelled.tryAcquire(TIMEOUT_SEC, TimeUnit.SECONDS)).isTrue();
            jobExited.release();
            return new PreRebootResult(Status.STATUS_FINISHED);
        });

        OnUpdateReadyResponse response = Utils.getFuture(mPreRebootDexoptJob.onUpdateReady(
                null /* otaSlot */, false /* isUpdateEngineReady */, JobSynchronicity.ASYNC));
        Utils.getFuture(response.asynchronousJobScheduling());
        mPreRebootDexoptJob.onStartJobImpl(mJobService, mJobParameters);
        mPreRebootDexoptJob.onStopJobImpl(mJobParameters);

        // Check that `onStopJob` is really blocking. If it wasn't, the check below might still pass
        // due to a race, but we would have a flaky test.
        assertThat(jobExited.tryAcquire()).isTrue();

        mPreRebootStatsReporterHarness.recordFakeAfterRebootDataAndReport();
        mPreRebootStatsReporterHarness.verifyJobStats(Status.STATUS_FINISHED);
    }

    @Test
    public void testSyncCancel() throws Exception {
        Semaphore dexoptCancelled = new Semaphore(0);
        Semaphore jobExited = new Semaphore(0);
        when(mPreRebootDriver.run(any(), anyBoolean(), any(), any())).thenAnswer(invocation -> {
            var cancellationSignal = invocation.<CancellationSignal>getArgument(2);
            cancellationSignal.setOnCancelListener(() -> dexoptCancelled.release());
            assertThat(dexoptCancelled.tryAcquire(TIMEOUT_SEC, TimeUnit.SECONDS)).isTrue();
            jobExited.release();
            return new PreRebootResult(Status.STATUS_FINISHED);
        });

        OnUpdateReadyResponse response = Utils.getFuture(mPreRebootDexoptJob.onUpdateReady(
                null /* otaSlot */, true /* isUpdateEngineReady */, JobSynchronicity.SYNC));
        mPreRebootDexoptJob.cancelGiven(response.synchronousJob(), false /* expectInterrupt */);

        // Check that `cancelGiven` is really blocking. If it wasn't, the check below might still
        // pass due to a race, but we would have a flaky test.
        assertThat(jobExited.tryAcquire()).isTrue();

        mPreRebootStatsReporterHarness.recordFakeAfterRebootDataAndReport();
        mPreRebootStatsReporterHarness.verifyJobStats(Status.STATUS_FINISHED);
    }

    // Tests an end-to-end hybrid job.
    @Test
    public void testHybrid() throws Exception {
        // Set the synchronous time limit to 3 minutes.
        when(SystemProperties.getInt(eq("dalvik.vm.pr_dexopt_sync_time_limit_millis"), anyInt()))
                .thenReturn(180000);

        // Simulate a long running synchronous job that has to be cancelled.
        Semaphore dexoptStarted = new Semaphore(0);
        Semaphore dexoptCancelled = new Semaphore(0);
        doAnswer(invocation -> {
            var cancellationSignal = invocation.<CancellationSignal>getArgument(2);
            cancellationSignal.setOnCancelListener(() -> dexoptCancelled.release());
            dexoptStarted.release();
            assertThat(dexoptCancelled.tryAcquire(TIMEOUT_SEC, TimeUnit.SECONDS)).isTrue();
            return new PreRebootResult(Status.STATUS_FINISHED);
        })
                .when(mPreRebootDriver)
                .run(any(), anyBoolean(), any(), any());

        // An update arrives.
        OnUpdateReadyResponse response = Utils.getFuture(mPreRebootDexoptJob.onUpdateReady(
                "_b" /* otaSlot */, true /* isUpdateEngineReady */, JobSynchronicity.HYBRID));
        assertThat(dexoptStarted.tryAcquire(TIMEOUT_SEC, TimeUnit.SECONDS)).isTrue();

        // Given that the update engine is ready, the synchronous job should not call update_engine.
        verify(mUpdateEngine, never()).triggerPostinstall(any());

        // Verify that the synchronous job was started with the right arguments, especially the
        // right reason.
        ArgumentCaptor<CancellationSignal> cancellationSignalCaptor =
                ArgumentCaptor.forClass(CancellationSignal.class);
        verify(mPreRebootDriver)
                .run(eq("_b"), eq(false), cancellationSignalCaptor.capture(),
                        eq(ReasonMapping.REASON_PRE_REBOOT_DEXOPT_SYNC));

        // Timeout not reached yet. The synchronous job is still running, and the asynchronous job
        // has not been scheduled yet.
        mMockClock.advanceTime(179999);
        assertThat(cancellationSignalCaptor.getValue().isCanceled()).isFalse();
        assertThat(response.asynchronousJobScheduling().isDone()).isFalse();

        // Timeout reached. The synchronous job is cancelled, and the asynchronous job is scheduled.
        mMockClock.advanceTime(1);
        assertThat(cancellationSignalCaptor.getValue().isCanceled()).isTrue();
        Utils.getFuture(response.synchronousJob());
        assertThat(Utils.getFuture(response.asynchronousJobScheduling()))
                .isEqualTo(ArtFlags.SCHEDULE_SUCCESS);

        // Simulate a successful asynchronous job.
        doReturn(new PreRebootResult(Status.STATUS_FINISHED))
                .when(mPreRebootDriver)
                .run(any(), anyBoolean(), any(), any());

        // The asynchronous job is started.
        mMockClock.advanceTime(123456);
        mPreRebootDexoptJob.onStartJobImpl(mJobService, mJobParameters);

        mPreRebootDexoptJob.waitForRunningJob();

        // The asynchronous job should call update_engine.
        verify(mUpdateEngine).triggerPostinstall("system");

        // Verify that the asynchronous job was started with the right arguments, especially the
        // right reason.
        verify(mPreRebootDriver)
                .run(eq("_b"), eq(false), any(), eq(ReasonMapping.REASON_PRE_REBOOT_DEXOPT));

        mPreRebootStatsReporterHarness.recordFakeAfterRebootDataAndReport();
        mPreRebootStatsReporterHarness.verifyJobStats(Status.STATUS_FINISHED);
        mPreRebootStatsReporterHarness.verifyJobLatency(123456);
        mPreRebootStatsReporterHarness.verifySynchronicity(JobSynchronicity.HYBRID);
    }

    // Tests a hybrid job where the synchronous job times out but the asynchronous job doesn't get a
    // chance to run.
    @Test
    public void testHybridSyncTimedOutButAsyncNotRun() throws Exception {
        // Set the synchronous time limit to 3 minutes.
        when(SystemProperties.getInt(eq("dalvik.vm.pr_dexopt_sync_time_limit_millis"), anyInt()))
                .thenReturn(180000);

        // Simulate a long running synchronous job that has to be cancelled.
        Semaphore dexoptStarted = new Semaphore(0);
        Semaphore dexoptCancelled = new Semaphore(0);
        when(mPreRebootDriver.run(any(), anyBoolean(), any(), any())).thenAnswer(invocation -> {
            var cancellationSignal = invocation.<CancellationSignal>getArgument(2);
            cancellationSignal.setOnCancelListener(() -> dexoptCancelled.release());
            dexoptStarted.release();
            assertThat(dexoptCancelled.tryAcquire(TIMEOUT_SEC, TimeUnit.SECONDS)).isTrue();
            return new PreRebootResult(Status.STATUS_FINISHED);
        });

        // An update arrives.
        OnUpdateReadyResponse response = Utils.getFuture(mPreRebootDexoptJob.onUpdateReady(
                "_b" /* otaSlot */, true /* isUpdateEngineReady */, JobSynchronicity.HYBRID));
        assertThat(dexoptStarted.tryAcquire(TIMEOUT_SEC, TimeUnit.SECONDS)).isTrue();

        ArgumentCaptor<CancellationSignal> cancellationSignalCaptor =
                ArgumentCaptor.forClass(CancellationSignal.class);
        verify(mPreRebootDriver)
                .run(any(), anyBoolean(), cancellationSignalCaptor.capture(), any());

        // Timeout reached. The synchronous job is cancelled, and the asynchronous job is scheduled.
        mMockClock.advanceTime(180000);
        assertThat(cancellationSignalCaptor.getValue().isCanceled()).isTrue();
        Utils.getFuture(response.synchronousJob());
        assertThat(Utils.getFuture(response.asynchronousJobScheduling()))
                .isEqualTo(ArtFlags.SCHEDULE_SUCCESS);

        // Simulate that the reboot happens before the asynchronous job gets a chance to run. The
        // hybrid job should be reported as partially finished.
        mPreRebootStatsReporterHarness.recordFakeAfterRebootDataAndReport();
        mPreRebootStatsReporterHarness.verifyJobStats(Status.STATUS_PARTIALLY_FINISHED);
        mPreRebootStatsReporterHarness.verifySynchronicity(JobSynchronicity.HYBRID);
    }

    // Tests a hybrid job where the synchronous job completes before the timeout.
    @Test
    public void testHybridSyncCompletesBeforeTimeout() throws Exception {
        // Set the synchronous time limit to 3 minutes.
        when(SystemProperties.getInt(eq("dalvik.vm.pr_dexopt_sync_time_limit_millis"), anyInt()))
                .thenReturn(180000);

        // Simulate a synchronous job that completes in 2 minutes.
        Semaphore dexoptStarted = new Semaphore(0);
        Semaphore dexoptDone = new Semaphore(0);
        when(mPreRebootDriver.run(any(), anyBoolean(), any(), any())).thenAnswer(invocation -> {
            mMockClock.getAsyncExecutor().executeDelayed(() -> dexoptDone.release(), 120000);
            dexoptStarted.release();
            assertThat(dexoptDone.tryAcquire(TIMEOUT_SEC, TimeUnit.SECONDS)).isTrue();
            return new PreRebootResult(Status.STATUS_FINISHED);
        });

        // An update arrives.
        OnUpdateReadyResponse response = Utils.getFuture(mPreRebootDexoptJob.onUpdateReady(
                "_b" /* otaSlot */, true /* isUpdateEngineReady */, JobSynchronicity.HYBRID));
        assertThat(dexoptStarted.tryAcquire(TIMEOUT_SEC, TimeUnit.SECONDS)).isTrue();

        // The synchronous job completes before the timeout. No asynchronous job is scheduled.
        mMockClock.advanceTime(120000);
        Utils.getFuture(response.synchronousJob());
        assertThat(Utils.getFuture(response.asynchronousJobScheduling())).isNull();

        mPreRebootStatsReporterHarness.recordFakeAfterRebootDataAndReport();
        mPreRebootStatsReporterHarness.verifyJobStats(Status.STATUS_FINISHED);
    }

    // Tests a hybrid job where the synchronous job fails.
    @Test
    public void testHybridSyncFailed() throws Exception {
        // Set the synchronous time limit to 3 minutes.
        when(SystemProperties.getInt(eq("dalvik.vm.pr_dexopt_sync_time_limit_millis"), anyInt()))
                .thenReturn(180000);

        // Simulate a long running synchronous job that has to be cancelled.
        Semaphore dexoptStarted = new Semaphore(0);
        Semaphore dexoptCancelled = new Semaphore(0);
        when(mPreRebootDriver.run(any(), anyBoolean(), any(), any())).thenAnswer(invocation -> {
            var cancellationSignal = invocation.<CancellationSignal>getArgument(2);
            cancellationSignal.setOnCancelListener(() -> dexoptCancelled.release());
            dexoptStarted.release();
            assertThat(dexoptCancelled.tryAcquire(TIMEOUT_SEC, TimeUnit.SECONDS)).isTrue();
            return new PreRebootResult(Status.STATUS_FINISHED);
        });

        // An update arrives.
        OnUpdateReadyResponse response = Utils.getFuture(mPreRebootDexoptJob.onUpdateReady(
                "_b" /* otaSlot */, true /* isUpdateEngineReady */, JobSynchronicity.HYBRID));
        assertThat(dexoptStarted.tryAcquire(TIMEOUT_SEC, TimeUnit.SECONDS)).isTrue();

        ArgumentCaptor<CancellationSignal> cancellationSignalCaptor =
                ArgumentCaptor.forClass(CancellationSignal.class);
        verify(mPreRebootDriver)
                .run(any(), anyBoolean(), cancellationSignalCaptor.capture(), any());

        // Simulate that the synchronous job is cancelled by update engine.
        cancellationSignalCaptor.getValue().cancel();

        // The synchronous job is cancelled. No asynchronous job is scheduled.
        Utils.getFuture(response.synchronousJob());
        assertThat(Utils.getFuture(response.asynchronousJobScheduling())).isNull();

        mPreRebootStatsReporterHarness.recordFakeAfterRebootDataAndReport();
        mPreRebootStatsReporterHarness.verifyJobStats(Status.STATUS_FINISHED);
    }

    // Tests a hybrid job where the synchronous job is cancelled by update engine.
    @Test
    public void testHybridSyncCancelledByUpdateEngine() throws Exception {
        // Set the synchronous time limit to 3 minutes.
        when(SystemProperties.getInt(eq("dalvik.vm.pr_dexopt_sync_time_limit_millis"), anyInt()))
                .thenReturn(180000);

        // Simulate a synchronous job that fails.
        when(mPreRebootDriver.run(any(), anyBoolean(), any(), any()))
                .thenReturn(new PreRebootResult(Status.STATUS_FAILED));

        // An update arrives.
        OnUpdateReadyResponse response = Utils.getFuture(mPreRebootDexoptJob.onUpdateReady(
                "_b" /* otaSlot */, true /* isUpdateEngineReady */, JobSynchronicity.HYBRID));

        // The synchronous job fails. No asynchronous job is scheduled.
        Utils.getFuture(response.synchronousJob());
        assertThat(Utils.getFuture(response.asynchronousJobScheduling())).isNull();

        mPreRebootStatsReporterHarness.recordFakeAfterRebootDataAndReport();
        mPreRebootStatsReporterHarness.verifyJobStats(Status.STATUS_FAILED);
    }

    @Test
    public void testUpdateOtaSlotOtaThenMainline() {
        OnUpdateReadyResponse response = Utils.getFuture(mPreRebootDexoptJob.onUpdateReady(
                "_b" /* otaSlot */, false /* isUpdateEngineReady */, JobSynchronicity.ASYNC));
        Utils.getFuture(response.asynchronousJobScheduling());
        response = Utils.getFuture(mPreRebootDexoptJob.onUpdateReady(
                null /* otaSlot */, false /* isUpdateEngineReady */, JobSynchronicity.ASYNC));
        Utils.getFuture(response.asynchronousJobScheduling());

        when(mPreRebootDriver.run(eq("_b"), anyBoolean(), any(), any()))
                .thenReturn(new PreRebootResult(Status.STATUS_FINISHED));

        mPreRebootDexoptJob.onStartJobImpl(mJobService, mJobParameters);
        mPreRebootDexoptJob.waitForRunningJob();
    }

    @Test
    public void testUpdateOtaSlotMainlineThenOta() {
        OnUpdateReadyResponse response = Utils.getFuture(mPreRebootDexoptJob.onUpdateReady(
                null /* otaSlot */, false /* isUpdateEngineReady */, JobSynchronicity.ASYNC));
        Utils.getFuture(response.asynchronousJobScheduling());
        response = Utils.getFuture(mPreRebootDexoptJob.onUpdateReady(
                "_a" /* otaSlot */, false /* isUpdateEngineReady */, JobSynchronicity.ASYNC));
        Utils.getFuture(response.asynchronousJobScheduling());

        when(mPreRebootDriver.run(eq("_a"), anyBoolean(), any(), any()))
                .thenReturn(new PreRebootResult(Status.STATUS_FINISHED));

        mPreRebootDexoptJob.onStartJobImpl(mJobService, mJobParameters);
        mPreRebootDexoptJob.waitForRunningJob();
    }

    @Test
    public void testUpdateOtaSlotMainlineThenMainline() {
        OnUpdateReadyResponse response = Utils.getFuture(mPreRebootDexoptJob.onUpdateReady(
                null /* otaSlot */, false /* isUpdateEngineReady */, JobSynchronicity.ASYNC));
        Utils.getFuture(response.asynchronousJobScheduling());
        response = Utils.getFuture(mPreRebootDexoptJob.onUpdateReady(
                null /* otaSlot */, false /* isUpdateEngineReady */, JobSynchronicity.ASYNC));
        Utils.getFuture(response.asynchronousJobScheduling());

        when(mPreRebootDriver.run(isNull(), anyBoolean(), any(), any()))
                .thenReturn(new PreRebootResult(Status.STATUS_FINISHED));

        mPreRebootDexoptJob.onStartJobImpl(mJobService, mJobParameters);
        mPreRebootDexoptJob.waitForRunningJob();
    }

    @Test
    public void testUpdateOtaSlotOtaThenOta() {
        OnUpdateReadyResponse response = Utils.getFuture(mPreRebootDexoptJob.onUpdateReady(
                "_b" /* otaSlot */, false /* isUpdateEngineReady */, JobSynchronicity.ASYNC));
        Utils.getFuture(response.asynchronousJobScheduling());
        response = Utils.getFuture(mPreRebootDexoptJob.onUpdateReady(
                "_b" /* otaSlot */, false /* isUpdateEngineReady */, JobSynchronicity.ASYNC));
        Utils.getFuture(response.asynchronousJobScheduling());

        when(mPreRebootDriver.run(eq("_b"), anyBoolean(), any(), any()))
                .thenReturn(new PreRebootResult(Status.STATUS_FINISHED));

        mPreRebootDexoptJob.onStartJobImpl(mJobService, mJobParameters);
        mPreRebootDexoptJob.waitForRunningJob();
    }

    @Test(expected = IllegalStateException.class)
    public void testUpdateOtaSlotOtaThenOtaDifferentSlots() {
        OnUpdateReadyResponse response = Utils.getFuture(mPreRebootDexoptJob.onUpdateReady(
                "_b" /* otaSlot */, false /* isUpdateEngineReady */, JobSynchronicity.ASYNC));
        Utils.getFuture(response.asynchronousJobScheduling());
        response = Utils.getFuture(mPreRebootDexoptJob.onUpdateReady(
                "_a" /* otaSlot */, false /* isUpdateEngineReady */, JobSynchronicity.ASYNC));
        Utils.getFuture(response.asynchronousJobScheduling());
    }

    @Test(expected = IllegalStateException.class)
    public void testUpdateOtaSlotOtaBogusSlot() {
        OnUpdateReadyResponse response = Utils.getFuture(mPreRebootDexoptJob.onUpdateReady(
                "_bogus" /* otaSlot */, false /* isUpdateEngineReady */, JobSynchronicity.ASYNC));
        Utils.getFuture(response.asynchronousJobScheduling());
    }

    @Test
    public void testStatsReportingForSuperseded() throws Exception {
        OnUpdateReadyResponse response = Utils.getFuture(mPreRebootDexoptJob.onUpdateReady(
                null /* otaSlot */, false /* isUpdateEngineReady */, JobSynchronicity.ASYNC));
        Utils.getFuture(response.asynchronousJobScheduling());

        when(mArtd.checkPreRebootStagedFilesStatus())
                .thenReturn(TestingUtils.createPreRebootStagedFilesStatus(
                        false /* isCommittable */, mMockClock.currentTimeMillis()));
        mMockClock.advanceTime(600);

        response = Utils.getFuture(mPreRebootDexoptJob.onUpdateReady(
                "_a" /* otaSlot */, false /* isUpdateEngineReady */, JobSynchronicity.ASYNC));
        Utils.getFuture(response.asynchronousJobScheduling());

        mPreRebootStatsReporterHarness.verifyArtifactsStats(
                PreRebootStatsReporter.END_STATUS_SUPERSEDED, 600 /* ageMillis */);
        mPreRebootStatsReporterHarness.verifyTimes(1);
    }

    /**
     * Verifies that `jobFinished` is not mistakenly called for an old job after a new job is
     * started.
     */
    @Test
    public void testRace1() throws Exception {
        var dexoptCancelled = new Semaphore(0);
        var jobBlocker = new Semaphore(0);

        when(mPreRebootDriver.run(any(), anyBoolean(), any(), any())).thenAnswer(invocation -> {
            var cancellationSignal = invocation.<CancellationSignal>getArgument(2);
            cancellationSignal.setOnCancelListener(() -> dexoptCancelled.release());
            // Simulate that the job takes a while to exit, no matter it's cancelled or not.
            assertThat(jobBlocker.tryAcquire(TIMEOUT_SEC, TimeUnit.SECONDS)).isTrue();
            return new PreRebootResult(Status.STATUS_FINISHED);
        });

        // An update arrives. A job is scheduled.
        OnUpdateReadyResponse response = Utils.getFuture(mPreRebootDexoptJob.onUpdateReady(
                null /* otaSlot */, false /* isUpdateEngineReady */, JobSynchronicity.ASYNC));
        Utils.getFuture(response.asynchronousJobScheduling());

        // The job scheduler starts the job.
        mPreRebootDexoptJob.onStartJobImpl(mJobService, mJobParameters);

        var jobFinishedCalledAfterNewJobStarted = new Semaphore(0);

        var thread = new Thread(() -> {
            // Another update arrives. A new job is scheduled, replacing the old job. The old job
            // doesn't exit immediately, so this call is blocked.
            JobParameters oldParameters = mJobParameters;
            OnUpdateReadyResponse response2 = Utils.getFuture(mPreRebootDexoptJob.onUpdateReady(
                    null /* otaSlot */, false /* isUpdateEngineReady */, JobSynchronicity.ASYNC));
            Utils.getFuture(response2.asynchronousJobScheduling());

            // The job scheduler tries to cancel the old job because of the new update. This call
            // doesn't matter because the job has already been cancelled by ourselves during the
            // `onUpdateReadyImpl` call above.
            mPreRebootDexoptJob.onStopJobImpl(oldParameters);

            // The job scheduler starts the new job.
            mPreRebootDexoptJob.onStartJobImpl(mJobService, mJobParameters);

            doAnswer(invocation -> {
                jobFinishedCalledAfterNewJobStarted.release();
                return null;
            })
                    .when(mJobService)
                    .jobFinished(any(), anyBoolean());
        });
        thread.start();

        // Wait a while for `thread` to block on waiting for the old job to exit.
        assertThat(dexoptCancelled.tryAcquire(TIMEOUT_SEC, TimeUnit.SECONDS)).isTrue();

        // The old job now exits, unblocking `thread`.
        jobBlocker.release();
        thread.join();

        // Give it 1s for `jobFinished` to be potentially called. Either `jobFinished` is called
        // before the new job is started, or it should not be called.
        assertThat(jobFinishedCalledAfterNewJobStarted.tryAcquire(1, TimeUnit.SECONDS)).isFalse();

        // The new job now exits.
        jobBlocker.release();

        // `jobFinished` is called for the new job.
        assertThat(jobFinishedCalledAfterNewJobStarted.tryAcquire(TIMEOUT_SEC, TimeUnit.SECONDS))
                .isTrue();
    }

    /** Verifies that `onStartJob` for an old job is ignored after the old job is unscheduled. */
    @Test
    public void testRace2() throws Exception {
        // An update arrives. A job is scheduled.
        OnUpdateReadyResponse response = Utils.getFuture(mPreRebootDexoptJob.onUpdateReady(
                null /* otaSlot */, false /* isUpdateEngineReady */, JobSynchronicity.ASYNC));
        Utils.getFuture(response.asynchronousJobScheduling());
        JobParameters oldParameters = mJobParameters;

        // The job scheduler starts the job. In the meantime, another update arrives. It's not
        // possible that `onStartJob` is called for the old job after `onUpdateReadyImpl` is called
        // because `onUpdateReadyImpl` unschedules the old job. However, since both calls acquire a
        // lock, the order of execution may be reversed. When this happens, the `onStartJob` request
        // should not succeed.
        response = Utils.getFuture(mPreRebootDexoptJob.onUpdateReady(
                null /* otaSlot */, false /* isUpdateEngineReady */, JobSynchronicity.ASYNC));
        Utils.getFuture(response.asynchronousJobScheduling());
        mPreRebootDexoptJob.onStartJobImpl(mJobService, oldParameters);
        assertThat(mPreRebootDexoptJob.hasRunningJob()).isFalse();

        // The job scheduler starts the new job. This request should succeed.
        mPreRebootDexoptJob.onStartJobImpl(mJobService, mJobParameters);
        assertThat(mPreRebootDexoptJob.hasRunningJob()).isTrue();
    }

    /**
     * Verifies that `onStopJob` for an old job is ignored after a new synchronous job is started.
     */
    @Test
    public void testRace3() throws Exception {
        Semaphore dexoptCancelled = new Semaphore(0);
        Semaphore jobExited = new Semaphore(0);
        when(mPreRebootDriver.run(any(), anyBoolean(), any(), any())).thenAnswer(invocation -> {
            var cancellationSignal = invocation.<CancellationSignal>getArgument(2);
            cancellationSignal.setOnCancelListener(() -> dexoptCancelled.release());
            assertThat(dexoptCancelled.tryAcquire(TIMEOUT_SEC, TimeUnit.SECONDS)).isTrue();
            jobExited.release();
            return new PreRebootResult(Status.STATUS_FINISHED);
        });

        // An update arrives. A job is scheduled.
        OnUpdateReadyResponse response = Utils.getFuture(mPreRebootDexoptJob.onUpdateReady(
                null /* otaSlot */, false /* isUpdateEngineReady */, JobSynchronicity.ASYNC));
        Utils.getFuture(response.asynchronousJobScheduling());

        // The job scheduler starts the job.
        mPreRebootDexoptJob.onStartJobImpl(mJobService, mJobParameters);

        // Another update arrives, requesting a synchronous job run, replacing the old job. The new
        // job, which is synchronous, is started right after the old job is cancelled by
        // `onUpdateReady`, before the job scheduler calls `onStartJob`.
        JobParameters oldParameters = mJobParameters;
        response = Utils.getFuture(mPreRebootDexoptJob.onUpdateReady(
                null /* otaSlot */, true /* isUpdateEngineReady */, JobSynchronicity.SYNC));

        // The old job should be cancelled at this point.
        // This cannot be the new job having exited because jobs are serialized.
        assertThat(jobExited.tryAcquire()).isTrue();

        // The `onStopJob` call finally arrives. This call should be a no-op because the job has
        // already been cancelled by ourselves during the second `onUpdateReady` call above. It
        // should not cancel the new job.
        mPreRebootDexoptJob.onStopJobImpl(oldParameters);

        // The new job should not be cancelled.
        assertThat(jobExited.tryAcquire()).isFalse();

        // Now cancel the new job.
        mPreRebootDexoptJob.cancelGiven(response.synchronousJob(), false /* expectInterrupt */);

        // Now the new job should be cancelled.
        assertThat(jobExited.tryAcquire()).isTrue();
    }

    /**
     * Verifies that a cancellation request that arrives at the same time as the synchronous job
     * timeout should cancel both the synchronous and asynchronous jobs.
     */
    @Test
    public void testRace4() throws Exception {
        // Set the synchronous time limit to 3 minutes.
        when(SystemProperties.getInt(eq("dalvik.vm.pr_dexopt_sync_time_limit_millis"), anyInt()))
                .thenReturn(180000);

        // Simulate a long running synchronous job that has to be cancelled.
        Semaphore dexoptStarted = new Semaphore(0);
        Semaphore dexoptCancelled = new Semaphore(0);
        doAnswer(invocation -> {
            var cancellationSignal = invocation.<CancellationSignal>getArgument(2);
            cancellationSignal.setOnCancelListener(() -> dexoptCancelled.release());
            dexoptStarted.release();
            assertThat(dexoptCancelled.tryAcquire(TIMEOUT_SEC, TimeUnit.SECONDS)).isTrue();
            return new PreRebootResult(Status.STATUS_FINISHED);
        })
                .when(mPreRebootDriver)
                .run(any(), anyBoolean(), any(), any());

        // An update arrives, requesting a hybrid job run.
        OnUpdateReadyResponse response = Utils.getFuture(mPreRebootDexoptJob.onUpdateReady(
                null /* otaSlot */, true /* isUpdateEngineReady */, JobSynchronicity.HYBRID));
        assertThat(dexoptStarted.tryAcquire(TIMEOUT_SEC, TimeUnit.SECONDS)).isTrue();

        // The synchronous job times out.
        mMockClock.advanceTime(180000);

        // In the meantime, a cancellation request arrives. It should cancel both the synchronous
        // and asynchronous jobs, regardless of whether it's precessed before or after the timeout.
        mPreRebootDexoptJob.cancelAny();

        Utils.getFuture(response.synchronousJob());
        Utils.getFuture(response.asynchronousJobScheduling());

        // There should be no pending asynchronous job.
        assertThat(mJobScheduler.getPendingJob(JOB_ID)).isNull();
    }

    /**
     * Verifies that another update request that arrives at the same time as the synchronous job
     * timeout should result in correct stats reporting.
     */
    @Test
    public void testRace5() throws Exception {
        // Set the synchronous time limit to 3 minutes.
        when(SystemProperties.getInt(eq("dalvik.vm.pr_dexopt_sync_time_limit_millis"), anyInt()))
                .thenReturn(180000);

        // Simulate a long running synchronous job that has to be cancelled.
        Semaphore dexoptStarted = new Semaphore(0);
        Semaphore dexoptCancelled = new Semaphore(0);
        doAnswer(invocation -> {
            var cancellationSignal = invocation.<CancellationSignal>getArgument(2);
            cancellationSignal.setOnCancelListener(() -> dexoptCancelled.release());
            dexoptStarted.release();
            assertThat(dexoptCancelled.tryAcquire(TIMEOUT_SEC, TimeUnit.SECONDS)).isTrue();
            return new PreRebootResult(Status.STATUS_FINISHED);
        })
                .when(mPreRebootDriver)
                .run(any(), anyBoolean(), any(), any());

        // An update arrives, requesting a hybrid job run.
        OnUpdateReadyResponse response = Utils.getFuture(mPreRebootDexoptJob.onUpdateReady(
                null /* otaSlot */, true /* isUpdateEngineReady */, JobSynchronicity.HYBRID));
        assertThat(dexoptStarted.tryAcquire(TIMEOUT_SEC, TimeUnit.SECONDS)).isTrue();

        // 10 seconds later, the first staged file is created.
        mMockClock.advanceTime(10000);
        when(mArtd.checkPreRebootStagedFilesStatus())
                .thenReturn(TestingUtils.createPreRebootStagedFilesStatus(
                        false /* isCommittable */, mMockClock.currentTimeMillis()));

        // The synchronous job times out.
        mMockClock.advanceTime(170000);

        // In the meantime, another update arrives, requesting an asynchronous job run.
        response = Utils.getFuture(mPreRebootDexoptJob.onUpdateReady(
                null /* otaSlot */, true /* isUpdateEngineReady */, JobSynchronicity.ASYNC));

        // The stats reporter should conclude the first job.
        mPreRebootStatsReporterHarness.verifyJobStats(Status.STATUS_PARTIALLY_FINISHED);
        mPreRebootStatsReporterHarness.verifyArtifactsStats(
                PreRebootStatsReporter.END_STATUS_SUPERSEDED, 170000 /* ageMillis */);

        // The stats reporter should correctly hold the initial state for the second job, as it's
        // never run.
        mPreRebootStatsReporterHarness.recordFakeAfterRebootDataAndReport();
        mPreRebootStatsReporterHarness.verifyJobStats(Status.STATUS_SCHEDULED);

        // There's no more extra reporting.
        mPreRebootStatsReporterHarness.verifyTimes(2);
    }

    @Test
    public void testCheckStagedFilesAge() throws Exception {
        when(SystemProperties.getInt(eq("dalvik.vm.pr_dexopt_retention"), anyInt())).thenReturn(30);
        Duration createdAt =
                Duration.ofMillis(mMockClock.currentTimeMillis()).minusDays(30).plusMillis(1);
        when(mArtd.checkPreRebootStagedFilesStatus())
                .thenReturn(TestingUtils.createPreRebootStagedFilesStatus(
                        false /* isCommittable */, createdAt.toMillis()));

        assertThat(mPreRebootDexoptJob.checkStagedFilesAge())
                .isEqualTo(new StagedFilesAge(
                        Duration.ofDays(30).minusMillis(1) /* age */, false /* isExpired */));
    }

    @Test
    public void testCheckStagedFilesAgeExpired() throws Exception {
        when(SystemProperties.getInt(eq("dalvik.vm.pr_dexopt_retention"), anyInt())).thenReturn(30);
        Duration createdAt = Duration.ofMillis(mMockClock.currentTimeMillis()).minusDays(30);
        when(mArtd.checkPreRebootStagedFilesStatus())
                .thenReturn(TestingUtils.createPreRebootStagedFilesStatus(
                        false /* isCommittable */, createdAt.toMillis()));

        assertThat(mPreRebootDexoptJob.checkStagedFilesAge())
                .isEqualTo(new StagedFilesAge(Duration.ofDays(30) /* age */, true /* isExpired */));
    }

    @Test
    public void testCheckStagedFilesAgeMissing() throws Exception {
        lenient()
                .when(SystemProperties.getInt(eq("dalvik.vm.pr_dexopt_retention"), anyInt()))
                .thenReturn(30);
        when(mArtd.checkPreRebootStagedFilesStatus()).thenReturn(null);

        assertThat(mPreRebootDexoptJob.checkStagedFilesAge()).isEqualTo(null);
    }

    @Test
    public void testCheckStagedFilesAgeError() throws Exception {
        lenient()
                .when(SystemProperties.getInt(eq("dalvik.vm.pr_dexopt_retention"), anyInt()))
                .thenReturn(30);
        when(mArtd.checkPreRebootStagedFilesStatus()).thenThrow(ServiceSpecificException.class);

        assertThat(mPreRebootDexoptJob.checkStagedFilesAge()).isEqualTo(null);
    }
}
