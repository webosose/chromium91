// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/arc/arc_prefs.h"
#include "components/arc/session/arc_supervision_transition.h"

#include <string>

#include "components/guest_os/guest_os_prefs.h"
#include "components/prefs/pref_registry_simple.h"

namespace arc {
namespace prefs {

// ======== PROFILE PREFS ========
// See below for local state prefs.

// A bool preference indicating whether traffic other than the VPN connection
// set via kAlwaysOnVpnPackage should be blackholed.
const char kAlwaysOnVpnLockdown[] = "arc.vpn.always_on.lockdown";
// A string preference indicating the Android app that will be used for
// "Always On VPN". Should be empty if "Always On VPN" is not enabled.
const char kAlwaysOnVpnPackage[] = "arc.vpn.always_on.vpn_package";
// Stores the user id received from DM Server when enrolling a Play user on an
// Active Directory managed device. Used to report to DM Server that the account
// is still used.
const char kArcActiveDirectoryPlayUserId[] =
    "arc.active_directory_play_user_id";
// A preference to keep list of Android apps and their state.
const char kArcApps[] = "arc.apps";
// A preference to store backup and restore state for Android apps.
const char kArcBackupRestoreEnabled[] = "arc.backup_restore.enabled";
// A preference to indicate that Android's data directory should be removed.
const char kArcDataRemoveRequested[] = "arc.data.remove_requested";
// A preference representing whether a user has opted in to use Google Play
// Store on ARC.
// TODO(hidehiko): For historical reason, now the preference name does not
// directly reflect "Google Play Store". We should get and set the values via
// utility methods (IsArcPlayStoreEnabledForProfile() and
// SetArcPlayStoreEnabledForProfile()) in chrome/browser/ash/arc/arc_util.h.
const char kArcEnabled[] = "arc.enabled";
// A preference to control if ARC can access removable media on the host side.
// TODO(fukino): Remove this pref once "Play Store applications can't access
// this device" toast in Files app becomes aware of kArcVisibleExternalStorages.
// crbug.com/998512.
const char kArcHasAccessToRemovableMedia[] =
    "arc.has_access_to_removable_media";
// A preference to keep list of external storages which are visible to Android
// apps. (i.e. can be read/written by Android apps.)
const char kArcVisibleExternalStorages[] = "arc.visible_external_storages";
// A preference that indicates that initial settings need to be applied. Initial
// settings are applied only once per new OptIn once mojo settings instance is
// ready. Each OptOut resets this preference. Note, its sense is close to
// |kArcSignedIn|, however due the asynchronous nature of initializing mojo
// components, timing of triggering |kArcSignedIn| and
// |kArcInitialSettingsPending| can be different and
// |kArcInitialSettingsPending| may even be handled in the next user session.
const char kArcInitialSettingsPending[] = "arc.initial.settings.pending";
// A preference that indicated whether Android reported it's compliance status
// with provided policies. This is used only as a signal to start Android kiosk.
const char kArcPolicyComplianceReported[] = "arc.policy_compliance_reported";
// A preference that indicates that a supervision transition is necessary, in
// response to a CHILD_ACCOUNT transiting to a REGULAR_ACCOUNT or vice-versa.
const char kArcSupervisionTransition[] = "arc.supervision_transition";
// A preference that indicates that user accepted PlayStore terms.
const char kArcTermsAccepted[] = "arc.terms.accepted";
// A preference that indicates that ToS was shown in OOBE flow.
const char kArcTermsShownInOobe[] = "arc.terms.shown_in_oobe";
// A preference to keep user's consent to use location service.
const char kArcLocationServiceEnabled[] = "arc.location_service.enabled";
// A preference to keep list of Android packages and their infomation.
const char kArcPackages[] = "arc.packages";
// A preference that indicates that Play Auto Install flow was already started.
const char kArcPaiStarted[] = "arc.pai.started";
// A preference that indicates that provisioning was initiated from OOBE. This
// is preserved across Chrome restart.
const char kArcProvisioningInitiatedFromOobe[] =
    "arc.provisioning.initiated.from.oobe";
// A preference that indicates that Play Fast App Reinstall flow was already
// started.
const char kArcFastAppReinstallStarted[] = "arc.fast.app.reinstall.started";
// A preference to keep list of Play Fast App Reinstall packages.
const char kArcFastAppReinstallPackages[] = "arc.fast.app.reinstall.packages";
// A preference to keep the current Android framework version. Note, that value
// is only available after first packages update.
const char kArcFrameworkVersion[] = "arc.framework.version";
// A preference that holds the list of apps that the admin requested to be
// push-installed.
const char kArcPushInstallAppsRequested[] = "arc.push_install.requested";
// A preference that holds the list of apps that the admin requested to be
// push-installed, but which have not been successfully installed yet.
const char kArcPushInstallAppsPending[] = "arc.push_install.pending";
// A preference to keep deferred requests of setting notifications enabled flag.
const char kArcSetNotificationsEnabledDeferred[] =
    "arc.set_notifications_enabled_deferred";
// A preference that indicates status of Android sign-in.
const char kArcSignedIn[] = "arc.signedin";
// A preference that indicates that ARC skipped the setup UI flows that
// contain a notice related to reporting of diagnostic information.
const char kArcSkippedReportingNotice[] = "arc.skipped.reporting.notice";
// A preference that indicates an ARC comaptible filesystem was chosen for
// the user directory (i.e., the user finished required migration.)
const char kArcCompatibleFilesystemChosen[] =
    "arc.compatible_filesystem.chosen";
// Preferences for storing engagement time data, as per
// GuestOsEngagementMetrics.
const char kEngagementPrefsPrefix[] = "arc.metrics";

// ======== LOCAL STATE PREFS ========

// A boolean preference that indicates whether this device has run with the
// native bridge 64 bit support experiment enabled. Persisting value in local
// state, rather than in a user profile, is required as it needs to be read
// whenever ARC mini-container is started.
const char kNativeBridge64BitSupportExperimentEnabled[] =
    "arc.native_bridge_64bit_support_experiment";

// A dictionary preference that keeps track of stability metric values, which is
// maintained by StabilityMetricsManager. Persisting values in local state is
// required to include these metrics in the initial stability log in case of a
// crash.
const char kStabilityMetrics[] = "arc.metrics.stability";

// A preference to keep the salt for generating ro.serialno and ro.boot.serialno
// Android properties. Used only in ARCVM.
const char kArcSerialNumberSalt[] = "arc.serialno_salt";

// A preference to keep time intervals when snapshotting is allowed.
const char kArcSnapshotHours[] = "arc.snapshot_hours";

// A preferece to keep ARC snapshot related info in dictionary.
const char kArcSnapshotInfo[] = "arc.snapshot";

void RegisterLocalStatePrefs(PrefRegistrySimple* registry) {
  // Sorted in lexicographical order.
  registry->RegisterStringPref(kArcSerialNumberSalt, std::string());
  registry->RegisterDictionaryPref(kArcSnapshotHours);
  registry->RegisterDictionaryPref(kArcSnapshotInfo);
  registry->RegisterBooleanPref(kNativeBridge64BitSupportExperimentEnabled,
                                false);
  registry->RegisterDictionaryPref(kStabilityMetrics);
}

void RegisterProfilePrefs(PrefRegistrySimple* registry) {
  // TODO(dspaid): Implement a mechanism to allow this to sync on first boot
  // only.

  // This is used to delete the Play user ID if ARC is disabled for an
  // Active Directory managed device.
  registry->RegisterStringPref(kArcActiveDirectoryPlayUserId, std::string());

  // Note that ArcBackupRestoreEnabled and ArcLocationServiceEnabled prefs have
  // to be off by default, until an explicit gesture from the user to enable
  // them is received. This is crucial in the cases when these prefs transition
  // from a previous managed state to the unmanaged.
  registry->RegisterBooleanPref(kArcBackupRestoreEnabled, false);
  registry->RegisterBooleanPref(kArcLocationServiceEnabled, false);

  registry->RegisterIntegerPref(
      kArcSupervisionTransition,
      static_cast<int>(ArcSupervisionTransition::NO_TRANSITION));

  guest_os::prefs::RegisterEngagementProfilePrefs(registry,
                                                  kEngagementPrefsPrefix);

  // Sorted in lexicographical order.
  registry->RegisterBooleanPref(kAlwaysOnVpnLockdown, false);
  registry->RegisterStringPref(kAlwaysOnVpnPackage, std::string());
  registry->RegisterBooleanPref(kArcDataRemoveRequested, false);
  registry->RegisterBooleanPref(kArcEnabled, false);
  registry->RegisterBooleanPref(kArcHasAccessToRemovableMedia, false);
  registry->RegisterBooleanPref(kArcInitialSettingsPending, false);
  registry->RegisterBooleanPref(kArcPaiStarted, false);
  registry->RegisterBooleanPref(kArcFastAppReinstallStarted, false);
  registry->RegisterListPref(kArcFastAppReinstallPackages);
  registry->RegisterBooleanPref(kArcPolicyComplianceReported, false);
  registry->RegisterBooleanPref(kArcProvisioningInitiatedFromOobe, false);
  registry->RegisterBooleanPref(kArcSignedIn, false);
  registry->RegisterBooleanPref(kArcSkippedReportingNotice, false);
  registry->RegisterBooleanPref(kArcTermsAccepted, false);
  registry->RegisterBooleanPref(kArcTermsShownInOobe, false);
  registry->RegisterListPref(kArcVisibleExternalStorages);
}

}  // namespace prefs
}  // namespace arc
