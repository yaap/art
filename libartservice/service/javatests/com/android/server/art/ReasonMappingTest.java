/*
 * Copyright (C) 2022 The Android Open Source Project
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
 * limitations under the License
 */

package com.android.server.art;

import static android.platform.test.flag.junit.DeviceFlagsValueProvider.createCheckFlagsRule;

import static com.android.server.art.testing.TestDataHelper.newPackageState;
import static com.android.server.art.testing.TestDataHelper.newUserState;
import static com.android.server.art.testing.TestingUtils.FLAGS_PREFIX;

import static com.google.common.truth.Truth.assertThat;

import static org.mockito.Mockito.any;
import static org.mockito.Mockito.anyBoolean;
import static org.mockito.Mockito.anyInt;
import static org.mockito.Mockito.anyLong;
import static org.mockito.Mockito.eq;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.when;

import android.apphibernation.AppHibernationManager;
import android.os.SystemProperties;
import android.os.UserHandle;
import android.os.UserManager;
import android.platform.test.annotations.RequiresFlagsDisabled;
import android.platform.test.annotations.RequiresFlagsEnabled;
import android.platform.test.flag.junit.CheckFlagsRule;

import androidx.test.filters.SmallTest;
import androidx.test.runner.AndroidJUnit4;

import com.android.art.rw.flags.Flags;
import com.android.modules.utils.pm.PackageStateModulesUtils;
import com.android.server.art.model.ArtFlags;
import com.android.server.art.testing.MockClock;
import com.android.server.art.testing.StaticMockitoRule;
import com.android.server.art.testing.TestDataHelper.PackageStateBuilder;
import com.android.server.pm.PackageManagerLocal;
import com.android.server.pm.pkg.PackageState;
import com.android.server.pm.pkg.PackageUserState;

import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;

import java.util.List;
import java.util.concurrent.TimeUnit;
import java.util.stream.Collectors;

@SmallTest
@RunWith(AndroidJUnit4.class)
public class ReasonMappingTest {
    private static final String PKG_NAME_1 = "com.example.foo";
    private static final String PKG_NAME_2 = "com.android.bar";
    private static final String PKG_NAME_3 = "com.android.baz";
    private static final String PKG_NAME_4 = "com.android.qux";
    private static final String PKG_NAME_HIBERNATING = "com.example.hibernating";
    private static final int INACTIVE_DAYS = 1;
    private static final long CURRENT_TIME_MS = 10000000000L;
    private static final long RECENT_TIME_MS =
            CURRENT_TIME_MS - TimeUnit.DAYS.toMillis(INACTIVE_DAYS) + 1;
    private static final long NOT_RECENT_TIME_MS =
            CURRENT_TIME_MS - TimeUnit.DAYS.toMillis(INACTIVE_DAYS) - 1;

    @Rule
    public StaticMockitoRule mockitoRule =
            new StaticMockitoRule(SystemProperties.class, PackageStateModulesUtils.class);

    @Rule public final CheckFlagsRule checkFlagsRule = createCheckFlagsRule();

    @Mock private ReasonMapping.Injector mInjector;
    @Mock private AppHibernationManager mAppHibernationManager;
    @Mock private UserManager mUserManager;
    @Mock private DexUseManagerLocal mDexUseManager;
    @Mock private PackageManagerLocal.FilteredSnapshot mSnapshot;
    private MockClock mMockClock;
    private ReasonMapping mReasonMapping;

    @Before
    public void setUp() throws Exception {
        mMockClock = new MockClock();

        lenient().when(mInjector.getAppHibernationManager()).thenReturn(mAppHibernationManager);
        lenient().when(mInjector.getUserManager()).thenReturn(mUserManager);
        lenient().when(mInjector.getDexUseManager()).thenReturn(mDexUseManager);
        lenient().when(mInjector.isSystemUiPackage(any())).thenReturn(false);
        lenient().when(mInjector.isLauncherPackage(any())).thenReturn(false);
        lenient().when(mInjector.getClock()).thenReturn(mMockClock);

        // Set up two users.
        lenient()
                .when(mUserManager.getUserHandles(anyBoolean()))
                .thenReturn(List.of(UserHandle.of(0), UserHandle.of(1)));

        // All packages are by default not hibernating.
        lenient().when(mAppHibernationManager.isHibernatingGlobally(any())).thenReturn(false);
        lenient().when(mAppHibernationManager.isOatArtifactDeletionEnabled()).thenReturn(true);

        List<PackageState> pkgStates = createPackageStates();
        var packageStateMap = pkgStates.stream().collect(
                Collectors.toMap(PackageState::getPackageName, it -> it));
        lenient().when(mSnapshot.getPackageStates()).thenReturn(packageStateMap);
        for (PackageState pkgState : pkgStates) {
            lenient()
                    .when(mSnapshot.getPackageState(pkgState.getPackageName()))
                    .thenReturn(pkgState);
        }

        lenient()
                .when(SystemProperties.getInt(
                        eq("pm.dexopt.downgrade_after_inactive_days"), anyInt()))
                .thenReturn(INACTIVE_DAYS);

        mReasonMapping = new ReasonMapping(mInjector);
    }

    @Test
    public void testGetCompilerFilterForReason() {
        when(SystemProperties.get("pm.dexopt.foo")).thenReturn("speed");
        assertThat(ReasonMapping.getCompilerFilterForReason("foo")).isEqualTo("speed");
    }

    @Test(expected = IllegalStateException.class)
    public void testGetCompilerFilterForReasonInvalidFilter() throws Exception {
        when(SystemProperties.get("pm.dexopt.foo")).thenReturn("invalid-filter");
        ReasonMapping.getCompilerFilterForReason("foo");
    }

    @Test(expected = IllegalArgumentException.class)
    public void testGetCompilerFilterForReasonInvalidReason() throws Exception {
        ReasonMapping.getCompilerFilterForReason("foo");
    }

    @Test
    public void testGetPriorityClassForReason() throws Exception {
        assertThat(ReasonMapping.getPriorityClassForReason("install"))
                .isEqualTo(ArtFlags.PRIORITY_INTERACTIVE);
    }

    @Test(expected = IllegalArgumentException.class)
    public void testGetPriorityClassForReasonInvalidReason() throws Exception {
        ReasonMapping.getPriorityClassForReason("foo");
    }

    @Test
    public void testGetConcurrencyForReason() {
        lenient()
                .when(SystemProperties.getInt(eq("pm.dexopt.bg-dexopt.concurrency"), anyInt()))
                .thenReturn(3);
        assertThat(ReasonMapping.getConcurrencyForReason("bg-dexopt")).isEqualTo(3);
    }

    @Test
    @RequiresFlagsDisabled(FLAGS_PREFIX + Flags.FLAG_HYBRID_PRE_REBOOT_DEXOPT)
    public void testGetDefaultPackagesForReasonDefaultScoreFlagDisabled() throws Exception {
        mMockClock.setCurrentTimeMillis(CURRENT_TIME_MS);

        lenient()
                .when(mDexUseManager.calculateDecayedPackageScore(eq(PKG_NAME_1), anyLong()))
                .thenReturn(1D);
        lenient()
                .when(mDexUseManager.calculateDecayedPackageScore(eq(PKG_NAME_2), anyLong()))
                .thenReturn(2D);
        lenient()
                .when(mDexUseManager.calculateDecayedPackageScore(eq(PKG_NAME_3), anyLong()))
                .thenReturn(0D);
        lenient()
                .when(mDexUseManager.calculateDecayedPackageScore(eq(PKG_NAME_4), anyLong()))
                .thenReturn(0.2);

        when(mDexUseManager.getPackageLastUsedAtMillis(PKG_NAME_1)).thenReturn(RECENT_TIME_MS);
        when(mDexUseManager.getPackageLastUsedAtMillis(PKG_NAME_2)).thenReturn(CURRENT_TIME_MS);

        // This package is recently installed but hasn't been used.
        PackageUserState userState =
                mSnapshot.getPackageState(PKG_NAME_3).getStateForUser(UserHandle.of(1));
        when(userState.getFirstInstallTimeMillis()).thenReturn(RECENT_TIME_MS + 1);
        when(mDexUseManager.getPackageLastUsedAtMillis(PKG_NAME_3)).thenReturn(0l);

        // This package should not be dexopted because it's not recently used.
        when(mDexUseManager.getPackageLastUsedAtMillis(PKG_NAME_4)).thenReturn(RECENT_TIME_MS - 1);

        // The list is sorted by last active time in descending order.
        assertThat(mReasonMapping.getDefaultPackagesForReason(mSnapshot, "bg-dexopt"))
                .containsExactly(PKG_NAME_2, PKG_NAME_3, PKG_NAME_1)
                .inOrder();
    }

    @Test
    @RequiresFlagsEnabled(FLAGS_PREFIX + Flags.FLAG_HYBRID_PRE_REBOOT_DEXOPT)
    public void testGetDefaultPackagesForReasonDefault() throws Exception {
        mMockClock.setCurrentTimeMillis(CURRENT_TIME_MS);

        when(mDexUseManager.calculateDecayedPackageScore(eq(PKG_NAME_1), anyLong())).thenReturn(1D);
        when(mDexUseManager.calculateDecayedPackageScore(eq(PKG_NAME_2), anyLong())).thenReturn(2D);
        when(mDexUseManager.calculateDecayedPackageScore(eq(PKG_NAME_3), anyLong())).thenReturn(0D);
        when(mDexUseManager.calculateDecayedPackageScore(eq(PKG_NAME_4), anyLong()))
                .thenReturn(0.2);

        when(mDexUseManager.getPackageLastUsedAtMillis(PKG_NAME_1)).thenReturn(RECENT_TIME_MS);
        when(mDexUseManager.getPackageLastUsedAtMillis(PKG_NAME_2)).thenReturn(CURRENT_TIME_MS);

        // This package is recently installed but hasn't been used.
        PackageUserState userState =
                mSnapshot.getPackageState(PKG_NAME_3).getStateForUser(UserHandle.of(1));
        when(userState.getFirstInstallTimeMillis()).thenReturn(RECENT_TIME_MS + 1);
        when(mDexUseManager.getPackageLastUsedAtMillis(PKG_NAME_3)).thenReturn(0l);

        // This package should not be dexopted because it's not recently used.
        when(mDexUseManager.getPackageLastUsedAtMillis(PKG_NAME_4)).thenReturn(RECENT_TIME_MS - 1);

        // The list is sorted by last active time in descending order.
        assertThat(mReasonMapping.getDefaultPackagesForReason(mSnapshot, "bg-dexopt"))
                .containsExactly(PKG_NAME_2, PKG_NAME_1, PKG_NAME_3)
                .inOrder();
    }

    @Test
    @RequiresFlagsDisabled(FLAGS_PREFIX + Flags.FLAG_HYBRID_PRE_REBOOT_DEXOPT)
    public void testGetDefaultPackagesForReasonInactiveScoreFlagDisabled() throws Exception {
        mMockClock.setCurrentTimeMillis(CURRENT_TIME_MS);

        lenient()
                .when(mDexUseManager.calculateDecayedPackageScore(eq(PKG_NAME_1), anyLong()))
                .thenReturn(2D);
        lenient()
                .when(mDexUseManager.calculateDecayedPackageScore(eq(PKG_NAME_2), anyLong()))
                .thenReturn(1D);
        lenient()
                .when(mDexUseManager.calculateDecayedPackageScore(eq(PKG_NAME_3), anyLong()))
                .thenReturn(0.1);
        lenient()
                .when(mDexUseManager.calculateDecayedPackageScore(eq(PKG_NAME_4), anyLong()))
                .thenReturn(0.2);

        // This package should not be downgraded because it's recently used.
        when(mDexUseManager.getPackageLastUsedAtMillis(PKG_NAME_1)).thenReturn(RECENT_TIME_MS);

        // This package should not be downgraded because it's recently installed, even though it
        // hasn't been used yet.
        PackageUserState userState =
                mSnapshot.getPackageState(PKG_NAME_2).getStateForUser(UserHandle.of(1));
        when(userState.getFirstInstallTimeMillis()).thenReturn(RECENT_TIME_MS + 1);
        when(mDexUseManager.getPackageLastUsedAtMillis(PKG_NAME_2)).thenReturn(0l);

        // These packages should be downgraded because they are not recently used.
        when(mDexUseManager.getPackageLastUsedAtMillis(PKG_NAME_3)).thenReturn(RECENT_TIME_MS - 1);
        when(mDexUseManager.getPackageLastUsedAtMillis(PKG_NAME_4)).thenReturn(RECENT_TIME_MS - 2);

        // The list is sorted by last active time in ascending order.
        assertThat(mReasonMapping.getDefaultPackagesForReason(mSnapshot, "inactive"))
                .containsExactly(PKG_NAME_4, PKG_NAME_3)
                .inOrder();
    }

    @Test
    @RequiresFlagsEnabled(FLAGS_PREFIX + Flags.FLAG_HYBRID_PRE_REBOOT_DEXOPT)
    public void testGetDefaultPackagesForReasonInactive() throws Exception {
        mMockClock.setCurrentTimeMillis(CURRENT_TIME_MS);

        when(mDexUseManager.calculateDecayedPackageScore(eq(PKG_NAME_1), anyLong())).thenReturn(2D);
        when(mDexUseManager.calculateDecayedPackageScore(eq(PKG_NAME_2), anyLong())).thenReturn(1D);
        when(mDexUseManager.calculateDecayedPackageScore(eq(PKG_NAME_3), anyLong()))
                .thenReturn(0.1);
        when(mDexUseManager.calculateDecayedPackageScore(eq(PKG_NAME_4), anyLong()))
                .thenReturn(0.2);

        // This package should not be downgraded because it's recently used.
        when(mDexUseManager.getPackageLastUsedAtMillis(PKG_NAME_1)).thenReturn(RECENT_TIME_MS);

        // This package should not be downgraded because it's recently installed, even though it
        // hasn't been used yet.
        PackageUserState userState =
                mSnapshot.getPackageState(PKG_NAME_2).getStateForUser(UserHandle.of(1));
        when(userState.getFirstInstallTimeMillis()).thenReturn(RECENT_TIME_MS + 1);
        when(mDexUseManager.getPackageLastUsedAtMillis(PKG_NAME_2)).thenReturn(0l);

        // These packages should be downgraded because they are not recently used.
        when(mDexUseManager.getPackageLastUsedAtMillis(PKG_NAME_3)).thenReturn(RECENT_TIME_MS - 1);
        when(mDexUseManager.getPackageLastUsedAtMillis(PKG_NAME_4)).thenReturn(RECENT_TIME_MS - 2);

        // The list is sorted by score in ascending order.
        assertThat(mReasonMapping.getDefaultPackagesForReason(mSnapshot, "inactive"))
                .containsExactly(PKG_NAME_3, PKG_NAME_4)
                .inOrder();
    }

    @Test
    public void testGetDefaultPackagesForReasonFirstBoot() throws Exception {
        // On first-boot all packages haven't been used and first install time is
        // 0 which simulates case of system time being advanced by AlarmManagerService after package
        // installation.
        lenient()
                .when(mDexUseManager.calculateDecayedPackageScore(any(), anyLong()))
                .thenReturn(0D);
        lenient().when(mDexUseManager.getPackageLastUsedAtMillis(any())).thenReturn(0L);

        // All packages should be dexopted, in any order, because we don't have information to
        // filter or sort them.
        assertThat(mReasonMapping.getDefaultPackagesForReason(mSnapshot, "first-boot"))
                .containsExactly(PKG_NAME_1, PKG_NAME_2, PKG_NAME_3, PKG_NAME_4);
    }

    @Test
    public void testDexoptPackagesBootAfterMainlineUpdate() throws Exception {
        lenient().when(mInjector.isSystemUiPackage(PKG_NAME_1)).thenReturn(true);
        lenient().when(mInjector.isLauncherPackage(PKG_NAME_2)).thenReturn(true);

        // It should dexopt the system UI and the launcher, not other packages.
        assertThat(
                mReasonMapping.getDefaultPackagesForReason(mSnapshot, "boot-after-mainline-update"))
                .containsExactly(PKG_NAME_1, PKG_NAME_2);
    }

    @Test
    public void testDexoptPackagesBootAfterMainlineUpdatePackagesNotFound() throws Exception {
        // It should dexopt the system UI and the launcher, but they are not found.
        assertThat(
                mReasonMapping.getDefaultPackagesForReason(mSnapshot, "boot-after-mainline-update"))
                .isEmpty();
    }

    private List<PackageState> createPackageStates() {
        PackageState pkgState1 =
                newPackageStateWithDefaults(PKG_NAME_1).setDexoptable(true).build();

        PackageState pkgState2 =
                newPackageStateWithDefaults(PKG_NAME_2).setDexoptable(true).build();

        PackageState pkgState3 =
                newPackageStateWithDefaults(PKG_NAME_3).setDexoptable(true).build();

        PackageState pkgState4 =
                newPackageStateWithDefaults(PKG_NAME_4).setDexoptable(true).build();

        // This should not be dexopted because it's hibernating.
        PackageState pkgHibernatingState =
                newPackageStateWithDefaults(PKG_NAME_HIBERNATING).setDexoptable(true).build();
        lenient()
                .when(mAppHibernationManager.isHibernatingGlobally(PKG_NAME_HIBERNATING))
                .thenReturn(true);

        // This should not be dexopted because it's not dexoptable.
        PackageState nonDexoptablePkgState =
                newPackageStateWithDefaults("com.example.non-dexoptable")
                        .setDexoptable(false)
                        .build();

        return List.of(pkgState1, pkgState2, pkgState3, pkgState4, pkgHibernatingState,
                nonDexoptablePkgState);
    }

    private PackageStateBuilder newPackageStateWithDefaults(String packageName) {
        return newPackageState(packageName)
                .setUserState(0, newUserState().build())
                .setUserState(1, newUserState().build());
    }
}
