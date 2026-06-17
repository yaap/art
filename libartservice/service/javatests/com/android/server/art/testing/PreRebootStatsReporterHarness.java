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

package com.android.server.art.testing;

import static com.android.server.art.testing.TestingUtils.SYNC_EXECUTOR;

import static org.mockito.Mockito.anyInt;
import static org.mockito.Mockito.anyLong;
import static org.mockito.Mockito.eq;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;

import com.android.server.art.ArtStatsLog;
import com.android.server.art.PreRebootDexoptJob;
import com.android.server.art.prereboot.PreRebootStatsReporter;
import com.android.server.art.prereboot.PreRebootStatsReporter.Injector;
import com.android.server.art.proto.PreRebootStats;
import com.android.server.art.proto.PreRebootStats.Status;

import org.mockito.verification.VerificationMode;

import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStream;
import java.util.function.Supplier;

public class PreRebootStatsReporterHarness {
    private Injector mInjector = mock(Injector.class);

    public PreRebootStatsReporterHarness() throws Exception {
        File tempFile = File.createTempFile("pre-reboot-stats", ".pb");
        tempFile.deleteOnExit();

        lenient().when(mInjector.getFilename()).thenReturn(tempFile.getAbsolutePath());

        // Make asynchronous reporting synchronous.
        lenient().when(mInjector.getAsyncExecutor()).thenReturn(SYNC_EXECUTOR);
    }

    public Injector getInjector() {
        return mInjector;
    }

    public PreRebootStatsReporter createStatsReporter() {
        return new PreRebootStatsReporter(mInjector);
    }

    public void recordFakePreRebootData() throws Exception {
        try (OutputStream out = new FileOutputStream(mInjector.getFilename())) {
            PreRebootStats.newBuilder().setStatus(Status.STATUS_FINISHED).build().writeTo(out);
        }
    }

    public void recordFakeAfterRebootDataAndReport() {
        var statsAfterRebootSession = createStatsReporter().new AfterRebootSession();
        statsAfterRebootSession.recordArtifactsEndStatus(
                PreRebootStatsReporter.END_STATUS_MISSING, 0 /* ageMillis */);
        statsAfterRebootSession.reportAsync();
    }

    public void verifyJobStats(Status status) {
        verifyImpl(times(1),
                () -> eq(PreRebootStatsReporter.getStatusForStatsd(status)), () -> anyLong(),
                () -> anyInt(), () -> anyLong(), () -> anyInt());
    }

    public void verifyJobLatency(long latency) {
        verifyImpl(times(1), () -> anyInt(), () -> eq(latency), () -> anyInt(), () -> anyLong(),
                () -> anyInt());
    }

    public void verifyArtifactsStats(int endStatus, long ageMillis) {
        verifyImpl(times(1),
                () -> anyInt(), () -> anyLong(), () -> eq(endStatus), () -> eq(ageMillis),
                () -> anyInt());
    }

    public void verifySynchronicity(PreRebootDexoptJob.JobSynchronicity synchronicity) {
        int expected = PreRebootStatsReporter.getJobSynchronicityForStatsd(
                PreRebootStatsReporter.getJobSynchronicityForStatsProto(synchronicity));

        verifyImpl(times(1), () -> anyInt(), () -> anyLong(), () -> anyInt(), () -> anyLong(),
                () -> eq(expected));
    }

    public void verifyTimes(int n) {
        verifyImpl(times(n), () -> anyInt(), () -> anyLong(), () -> anyInt(), () -> anyLong(),
                () -> anyInt());
    }

    private void verifyImpl(VerificationMode mode, Supplier<Integer> statusMatcher,
            Supplier<Long> latencyMatcher, Supplier<Integer> artifactsEndStatusMatcher,
            Supplier<Long> artifactsAgeMillisMatcher, Supplier<Integer> synchronicityMatcher) {
        verify(mInjector, mode)
                .writeStats(eq(ArtStatsLog.PREREBOOT_DEXOPT_JOB_ENDED), statusMatcher.get(),
                        anyInt(), anyInt(), anyInt(), anyInt(), anyLong(), latencyMatcher.get(),
                        anyInt(), anyInt(), anyInt(), anyInt(), anyInt(), anyInt(),
                        artifactsEndStatusMatcher.get(), artifactsAgeMillisMatcher.get(),
                        synchronicityMatcher.get());
    }
}
