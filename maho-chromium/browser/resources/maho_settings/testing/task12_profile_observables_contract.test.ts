import {describe, expect, test} from 'bun:test';
import {readFileSync} from 'node:fs';
import {resolve} from 'node:path';

const settingsSourceRoot = resolve(
    import.meta.dir, '../../../ui/webui/maho_settings');
const readSettingsSource = (relativePath: string) =>
  readFileSync(resolve(settingsSourceRoot, relativePath), 'utf8');
const readTestSource = (relativePath: string) =>
  readFileSync(resolve(import.meta.dir, relativePath), 'utf8');

const mojom = readSettingsSource('maho_settings.mojom');
const handlerHeader = readSettingsSource('maho_settings_page_handler.h');
const handlerImplementation = readSettingsSource('maho_settings_page_handler.cc');
const ambientDeclarations = readTestSource('../maho_settings.mojom-webui.js.d.ts');


function requireMatch(
    source: string, pattern: RegExp, failureMessage: string): void {
  expect(source, failureMessage).toMatch(pattern);
}

function requireNoMatch(
    source: string, pattern: RegExp, failureMessage: string): void {
  expect(source, failureMessage).not.toMatch(pattern);
}

function extractBlock(source: string, declaration: RegExp, name: string): string {
  const match = declaration.exec(source);
  expect(match, `${name} must be declared`).not.toBeNull();
  return match?.[1] ?? '';
}

function findFunction(source: string, signature: string): string|null {
  const start = source.indexOf(signature);
  if (start < 0) {
    return null;
  }
  const bodyStart = source.indexOf('{', start);
  if (bodyStart < 0) {
    return null;
  }

  let depth = 0;
  for (let index = bodyStart; index < source.length; ++index) {
    if (source[index] === '{') {
      ++depth;
    } else if (source[index] === '}' && --depth === 0) {
      return source.slice(start, index + 1);
    }
  }
  return null;
}

function extractFunction(source: string, signature: string): string {
  const body = findFunction(source, signature);
  expect(body, `${signature} must have a complete definition`).not.toBeNull();
  return body ?? '';
}

describe('Task 12 profile observables source contract', () => {
  test('Mojo exposes lifecycle entries with exact, distinct states', () => {
    const lifecycleEntry = extractBlock(
        mojom,
        /struct\s+ProfileObservableLifecycleEntry\s*\{([\s\S]*?)\};/,
        'ProfileObservableLifecycleEntry');
    requireMatch(
        lifecycleEntry, /\bstring\s+profile_id\s*;/,
        'ProfileObservableLifecycleEntry must carry profile_id');

    const lifecycleEnum = extractBlock(
        mojom,
        /enum\s+ProfileObservableLifecycleState\s*\{([\s\S]*?)\};/,
        'ProfileObservableLifecycleState');
    const states = [...lifecycleEnum.matchAll(/^\s*(k[A-Za-z0-9_]+)\s*(?:=\s*[^,]+)?\s*,?\s*$/gm)]
                       .map(match => match[1]);
    expect(
        states,
        'ProfileObservableLifecycleState must contain exactly the four distinct observable lifecycle states')
        .toEqual(['kProvisioning', 'kReady', 'kDeleting', 'kRepairRequired']);
    expect(
        new Set(states).size,
        'ProfileObservableLifecycleState values must be distinct declarations')
        .toBe(4);
    requireMatch(
        lifecycleEntry,
        /\bProfileObservableLifecycleState\s+lifecycle_state\s*;/,
        'ProfileObservableLifecycleEntry must carry the typed lifecycle state');
  });

  test('Mojo snapshot exposes authoritative observable fields and explicit unavailable deleted history', () => {
    const snapshot = extractBlock(
        mojom,
        /struct\s+ProfileObservablesSnapshot\s*\{([\s\S]*?)\};/,
        'ProfileObservablesSnapshot');
    requireMatch(
        snapshot, /\bstring\?\s+active_browser_profile_id\s*;/,
        'ProfileObservablesSnapshot.active_browser_profile_id must be nullable');
    requireMatch(
        snapshot, /\bstring\?\s+active_maho_profile_id\s*;/,
        'ProfileObservablesSnapshot.active_maho_profile_id must be nullable');
    requireMatch(
        snapshot, /\b(?:u?int64|uint32)\s+registry_revision\s*;/,
        'ProfileObservablesSnapshot must carry the catalog registry revision');
    requireMatch(
        snapshot,
        /\barray<ProfileObservableLifecycleEntry>\s+(?:lifecycle_entries|profile_lifecycle_entries)\s*;/,
        'ProfileObservablesSnapshot must carry typed lifecycle entries');
    requireMatch(
        snapshot, /\barray<string>\s+pending_deletion_profile_ids\s*;/,
        'ProfileObservablesSnapshot must carry pending deletion profile IDs');
    requireMatch(
        snapshot,
        /\bDeletedProfileHistoryAvailability\s+deleted_history_availability\s*;/,
        'ProfileObservablesSnapshot must expose typed deleted-history availability');
    const deletedHistoryAvailability = extractBlock(
        mojom,
        /enum\s+DeletedProfileHistoryAvailability\s*\{([\s\S]*?)\};/,
        'DeletedProfileHistoryAvailability');
    const availabilityValues =
        [...deletedHistoryAvailability.matchAll(/^\s*(k[A-Za-z0-9_]+)\s*(?:=\s*[^,]+)?\s*,?\s*$/gm)]
            .map(match => match[1]);
    expect(
        availabilityValues,
        'Deleted history has no authoritative source, so the transport must expose exactly and only kUnavailable')
        .toEqual(['kUnavailable']);
    requireNoMatch(
        snapshot, /\barray<string>\s+deleted(?:_history)?_profile_ids\s*;/,
        'ProfileObservablesSnapshot must not claim an authoritative hardcoded deleted-profile array');
    requireMatch(
        mojom,
        /GetProfileObservablesSnapshot\s*\(\s*\)\s*=>\s*\(\s*ProfileObservablesSnapshot\?\s+snapshot\s*\)\s*;/,
        'GetProfileObservablesSnapshot must return an explicitly nullable snapshot');
    requireMatch(
        ambientDeclarations,
        /getProfileObservablesSnapshot\s*\(\s*\)\s*:\s*Promise<\s*\{\s*snapshot\s*:\s*ProfileObservablesSnapshot\s*\|\s*null\s*;?\s*\}\s*>\s*;/,
        'The ambient WebUI declaration must preserve the nullable snapshot result');
  });

  test('PageHandler asynchronously reads and consistently validates the profile snapshot', () => {
    requireMatch(
        handlerHeader, /\bGetProfileObservablesSnapshot\s*\(/,
        'PageHandler header must declare GetProfileObservablesSnapshot');
    requireMatch(
        handlerHeader, /\bStartProfileObservablesSnapshotRead\s*\(/,
        'PageHandler must declare the explicit snapshot read/retry helper');
    requireMatch(
        handlerHeader, /\bOnActiveMahoProfileIdRead\s*\(/,
        'PageHandler must declare the asynchronous core-read reply helper');
    requireMatch(
        handlerImplementation,
        /std::optional<std::string>\s+ReadActiveMahoProfileIdJsonOnCoreSequence\s*\(/,
        'The active Maho ID reader must return optional copied JSON bytes');
    requireMatch(
        handlerImplementation,
        /maho::PostCoreTask<std::optional<std::string>>\s*\(/,
        'The active Maho ID read must be posted to the core sequence');
    requireMatch(
        handlerImplementation,
        /base::BindOnce\(&MahoSettingsPageHandler::OnActiveMahoProfileIdRead,\s*weak_factory_\.GetWeakPtr\(\),\s*std::move\(callback\)/,
        'The core reply and pending Mojo callback must be owned by a weak handler binding');
    requireMatch(
        handlerImplementation, /\bmaho_core_get_active_profile_id\s*\(/,
        'The core-sequence helper must call maho_core_get_active_profile_id');
    requireMatch(
        handlerImplementation, /\bCanonicalizeProfileRegistryId\s*\(/,
        'Present core identity must be canonicalized against the captured catalog');
    requireMatch(
        handlerImplementation,
        /DecideProfileObservablesSnapshotForTesting\(\s*captured_catalog,\s*bridge->GetProfileCatalog\(\)\.revision/,
        'The reply helper must revalidate the captured catalog revision through the decision helper');
    requireMatch(
        handlerImplementation,
        /attempt\s*==\s*0[\s\S]{0,240}StartProfileObservablesSnapshotRead\([^;]+,\s*1\)/,
        'A stale catalog must retry exactly once from a fresh capture');
    requireMatch(
        handlerImplementation,
        /attempt\s*==\s*0[\s\S]{0,320}else\s*\{\s*std::move\(callback\)\.Run\(nullptr\)/,
        'A second stale read must fail closed with a null snapshot');
    requireMatch(
        handlerImplementation,
        /record\.maho_id\.empty\(\)[\s\S]{0,160}!profile_ids\.insert\(record\.maho_id\)\.second/,
        'The captured catalog must reject empty and duplicate Maho IDs');
    requireMatch(
        handlerImplementation, /browser_profile_match_count\s*>\s*1u/,
        'Multiple handler-profile basename matches must fail closed');
    requireMatch(
        handlerImplementation, /active_profile_count\s*!=\s*1u/,
        'The captured catalog must contain exactly one active record');
    requireMatch(
        handlerImplementation,
        /if\s*\(browser_profile_record\)\s*\{\s*snapshot->active_browser_profile_id\s*=\s*browser_profile_record->maho_id\s*;/,
        'Zero basename matches must leave active_browser_profile_id null');
    requireMatch(
        handlerImplementation,
        /if\s*\(active_profile_id_json\)\s*\{[\s\S]{0,700}CanonicalizeProfileRegistryId\([\s\S]{0,220}\*canonical_active_profile_id\s*!=\s*active_profile_record->maho_id/,
        'Present core JSON must decode, canonicalize to a known ID, and equal the unique catalog-active ID');
    requireMatch(
        handlerImplementation,
        /if\s*\(canonical_active_profile_id\)\s*\{\s*snapshot->active_maho_profile_id\s*=\s*\*canonical_active_profile_id\s*;/,
        'Absent core output must leave active_maho_profile_id null');
    for (const lifecycle of
             ['kProvisioning', 'kReady', 'kDeleting', 'kRepairRequired']) {
      requireMatch(
          handlerImplementation, new RegExp(`\\b${lifecycle}\\b`),
          `The snapshot helpers must preserve ${lifecycle} inventory entries`);
    }
    requireMatch(
        handlerImplementation,
        /pending_deletion_profile_ids[\s\S]{0,320}kDeleting|kDeleting[\s\S]{0,320}pending_deletion_profile_ids/,
        'Pending deletion IDs must be derived exactly from kDeleting catalog records');
    requireMatch(
        handlerImplementation,
        /deleted_history_availability\s*=\s*[^;]*kUnavailable\s*;/,
        'Deleted history must be explicitly kUnavailable, never fabricated as an empty history');
  });

  test('eligible profile deletion completes false when Chromium drops completion callback', () => {
    const coordinator = extractFunction(
        handlerImplementation, 'class ProfileDeletionCoordinator');
    const coordinatorDelete = extractFunction(coordinator, 'void Delete(');
    const deletionHandoff =
        coordinatorDelete.indexOf('MaybeScheduleProfileForDeletion(');
    const wrappedCallback =
        /mojo::WrapCallbackWithDefaultInvokeIfNotRun\(\s*std::move\(callback\),\s*false\s*\)/;
    const wrapperPosition = coordinatorDelete.search(wrappedCallback);

    expect(
        wrapperPosition >= 0 && wrapperPosition < deletionHandoff &&
            /pending_deletion_\.emplace\([\s\S]*mojo::WrapCallbackWithDefaultInvokeIfNotRun/.test(
                coordinatorDelete) &&
            /DeleteProfileCallback\s+callback\s*;/.test(coordinator) &&
            !/MahoSettingsPageHandler::OnChromiumProfileDeletionAccepted/.test(
                handlerImplementation),
        'ProfileDeletionCoordinator::Delete must retain a default-false Mojo callback before Chromium can drop its scheduling callback')
        .toBe(true);
  });

  test('profile deletion rolls back when Chromium drops an uncommitted schedule callback', () => {
    const coordinator = extractFunction(
        handlerImplementation, 'class ProfileDeletionCoordinator');
    const coordinatorDelete = extractFunction(coordinator, 'void Delete(');
    const schedulingSettledSignature =
        coordinator.match(/void\s+\w*(?:Scheduling|Schedule)\w*Settled\s*\([^)]*\)/)?.[0] ??
        '';
    const schedulingSettled = schedulingSettledSignature === '' ?
        '' :
        (findFunction(coordinator, schedulingSettledSignature) ?? '');
    const failures: string[] = [];
    const contract = (condition: boolean, message: string) => {
      if (!condition) {
        failures.push(message);
      }
    };

    contract(
        /MaybeScheduleProfileForDeletion\(\s*[^,]+,\s*mojo::WrapCallbackWithDefaultInvokeIfNotRun\(\s*base::BindOnce\(\s*&ProfileDeletionCoordinator::\w*(?:Scheduling|Schedule)\w*Settled\s*,[\s\S]*?\b(?:scheduled_)?path\b[\s\S]*?\)\s*,\s*(?:nullptr|static_cast<\s*Profile\s*\*\s*>\(nullptr\))\s*\)\s*,\s*ProfileMetrics::DELETE_PROFILE_SETTINGS\s*\)/.test(
            coordinatorDelete),
        'Delete must give Chromium the scheduled path, a default-invoked scheduling-settled callback, and the settings deletion source');
    contract(
        schedulingSettled !== '' &&
            /\bIsProfileDirectoryMarkedForDeletion\(\s*(?:scheduled_)?path\s*\)/.test(
                schedulingSettled) &&
            /if\s*\(\s*!\s*[^)]*IsProfileDirectoryMarkedForDeletion[\s\S]*?\)\s*\{?[\s\S]*?\bCompleteBeforeChromiumCommit\(\s*(?:scheduled_)?path\s*\)/.test(
                schedulingSettled),
        'the scheduling-settled method must roll back through CompleteBeforeChromiumCommit(path) when Chromium did not mark that directory for deletion');
    contract(
        !/MaybeScheduleProfileForDeletion\(\s*[^,]+,\s*base::DoNothing\(\)\s*\)/.test(
            coordinatorDelete),
        'Delete must not discard Chromium scheduling settlement with base::DoNothing()');

    expect(
        failures,
        'profile deletion must roll back if Chromium drops its callback before committing the directory deletion')
        .toEqual([]);
  });

  test('profile deletion remains single flight through core reconciliation', () => {
    const coordinator = extractFunction(
        handlerImplementation, 'class ProfileDeletionCoordinator');
    const coordinatorDelete = extractFunction(coordinator, 'void Delete(');
    const hasPendingDeletion =
        extractFunction(coordinator, 'bool HasPendingDeletion(');
    const onProfileWasRemoved =
        extractFunction(coordinator, 'void OnProfileWasRemoved(');
    const onCoreDeletionFinished =
        extractFunction(coordinator, 'void OnCoreDeletionFinished(');
    const inFlightState =
        coordinator.match(/\bbool\s+(\w*(?:in_flight|reconcil)\w*)\s*=\s*false\s*;/)?.[1] ??
        '';
    const escapedInFlightState =
        inFlightState.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
    const statePattern = escapedInFlightState === '' ?
        null :
        new RegExp(`\\b${escapedInFlightState}\\b`);
    const setPattern = escapedInFlightState === '' ?
        null :
        new RegExp(`\\b${escapedInFlightState}\\s*=\\s*true\\s*;`);
    const clearPattern = escapedInFlightState === '' ?
        null :
        new RegExp(`\\b${escapedInFlightState}\\s*=\\s*false\\s*;`);
    const failures: string[] = [];
    const contract = (condition: boolean, message: string) => {
      if (!condition) {
        failures.push(message);
      }
    };

    contract(
        inFlightState !== '' && statePattern?.test(hasPendingDeletion) === true &&
            /pending_deletion_/.test(hasPendingDeletion),
        'HasPendingDeletion must include explicit core-reconciliation in-flight state as well as pending Chromium removal state');
    contract(
        /if\s*\(\s*HasPendingDeletion\(\)\s*\)\s*\{[\s\S]{0,160}Run\(false\)/.test(
            coordinatorDelete),
        'Delete must reject concurrent requests through the shared pending-or-reconciling predicate');

    const inFlightSet = setPattern?.exec(onProfileWasRemoved)?.index ?? -1;
    const pendingMove = onProfileWasRemoved.indexOf(
        'PendingDeletion pending = std::move(*pending_deletion_)');
    const pendingClear = onProfileWasRemoved.indexOf('pending_deletion_.reset()');
    const corePost = onProfileWasRemoved.indexOf('maho::PostCoreTask');
    contract(
        inFlightSet >= 0 && pendingMove > inFlightSet &&
            pendingClear > inFlightSet && corePost > pendingClear,
        'OnProfileWasRemoved must enter core-reconciliation flight before moving or clearing pending state and before posting core work');

    const reconciliation =
        onCoreDeletionFinished.indexOf('ReconcileProfileRegistryFromCore()');
    const inFlightClear = clearPattern?.exec(onCoreDeletionFinished)?.index ?? -1;
    const callbackCompletion =
        onCoreDeletionFinished.search(/std::move\(pending\.callback\)\.Run\(/);
    contract(
        reconciliation >= 0 && inFlightClear > reconciliation &&
            callbackCompletion > inFlightClear,
        'OnCoreDeletionFinished must keep deletion in flight through reconciliation, clearing it only at callback completion');

    expect(
        failures,
        'profile deletion must remain single flight across Chromium removal and core reconciliation')
        .toEqual([]);
  });

  test('profile deletion completes only after ProfileAttributesStorage removal', () => {
    const coordinator = extractFunction(
        handlerImplementation, 'class ProfileDeletionCoordinator');
    const coordinatorDelete = extractFunction(coordinator, 'void Delete(');
    const onProfileWasRemoved =
        extractFunction(coordinator, 'void OnProfileWasRemoved(');
    const onCoreDeletionFinished =
        extractFunction(coordinator, 'void OnCoreDeletionFinished(');
    const onProfileManagerDestroying =
        extractFunction(coordinator, 'void OnProfileManagerDestroying(');
    const coordinatorDestructor =
        extractFunction(coordinator, '~ProfileDeletionCoordinator(');
    const handlerDelete = extractFunction(
        handlerImplementation, 'void MahoSettingsPageHandler::DeleteProfile(');
    const failures: string[] = [];
    const contract = (condition: boolean, message: string) => {
      if (!condition) {
        failures.push(message);
      }
    };

    contract(
        /class\s+ProfileDeletionCoordinator\s*:\s*public\s+ProfileAttributesStorage::Observer\s*,\s*public\s+ProfileManagerObserver/.test(
            coordinator),
        'the process-lifetime coordinator must observe ProfileAttributesStorage and ProfileManager');
    contract(
        /base::ScopedObservation<\s*ProfileAttributesStorage\s*,\s*ProfileAttributesStorage::Observer\s*>\s+profile_attributes_observation_\{this\}\s*;/.test(
            coordinator) &&
            /base::ScopedObservation<\s*ProfileManager\s*,\s*ProfileManagerObserver\s*>\s+profile_manager_observation_\{this\}\s*;/.test(
                coordinator),
        'the coordinator must own scoped storage and ProfileManager observations');
    contract(
        /struct\s+PendingDeletion[\s\S]*std::string\s+canonical_profile_id\s*;[\s\S]*base::FilePath\s+profile_path\s*;[\s\S]*DeleteProfileCallback\s+callback\s*;/.test(
            coordinator) &&
            /std::optional<\s*PendingDeletion\s*>\s+pending_deletion_\s*;/.test(
                coordinator),
        'coordinator pending state must retain canonical identity, exact Chromium path, and the Mojo callback');
    contract(
        /GetProfileAttributesStorage\(\)/.test(coordinatorDelete) &&
            /profile_attributes_observation_\.Observe\(storage\)/.test(
                coordinatorDelete) &&
            /profile_manager_observation_\.Observe\(profile_manager\)/.test(
                coordinatorDelete),
        'coordinator Delete must observe the current storage and ProfileManager');
    contract(
        /if\s*\(HasPendingDeletion\(\)\)\s*\{[\s\S]{0,120}Run\(false\)/.test(
            coordinatorDelete) &&
            /pending_deletion_\.emplace\([\s\S]*WrapCallbackWithDefaultInvokeIfNotRun\([\s\S]*std::move\(callback\)[\s\S]*false/.test(
                coordinatorDelete),
        'coordinator Delete must reject concurrent deletion and retain a wrapped default-false callback');
    contract(
        /SetProfileLifecycleState\([\s\S]*kDeleting/.test(coordinatorDelete) &&
            /MaybeScheduleProfileForDeletion\(\s*scheduled_path\s*,\s*mojo::WrapCallbackWithDefaultInvokeIfNotRun\(\s*base::BindOnce\(\s*&ProfileDeletionCoordinator::OnChromiumDeletionSchedulingSettled\s*,[\s\S]*?\bscheduled_path\b[\s\S]*?\)\s*,\s*nullptr\s*\)\s*,\s*ProfileMetrics::DELETE_PROFILE_SETTINGS\s*\)/.test(
                coordinatorDelete) &&
            !/OnChromiumProfileDeletionAccepted/.test(coordinatorDelete),
        'coordinator Delete must mark kDeleting and schedule Chromium through the default-invoked scheduling-settled callback, never the obsolete acceptance binding');

    contract(
        /CanonicalizeProfileId\(profile_id\)/.test(handlerDelete) &&
            /record->is_default/.test(handlerDelete) &&
            /record->lifecycle\s*!=\s*maho::ProfileLifecycleState::kReady/.test(
                handlerDelete) &&
            /deletion_coordinator->HasPendingDeletion\(\)/.test(handlerDelete),
        'handler DeleteProfile must validate eligibility and reject while the coordinator is pending');
    contract(
        /selected_profile_id_\s*==\s*\*canonical_id[\s\S]{0,120}InvalidateSelectedProfileContext\(\)/.test(
            handlerDelete) &&
            /(?:deletion_coordinator|ProfileDeletionCoordinator::Get\(\))->Delete\(\s*profile_manager\s*,\s*\*canonical_id\s*,\s*path\s*,\s*std::move\(callback\)\s*\)/.test(
                handlerDelete),
        'handler DeleteProfile must invalidate selected context and delegate completion ownership to the coordinator');
    contract(
        !/MaybeScheduleProfileForDeletion|WrapCallbackWithDefaultInvokeIfNotRun|DeleteProfileOnCoreSequence/.test(
            handlerDelete),
        'handler DeleteProfile must not own Chromium scheduling, callback wrapping, or core deletion completion');

    const pendingMove = onProfileWasRemoved.indexOf(
        'PendingDeletion pending = std::move(*pending_deletion_)');
    const pendingClear = onProfileWasRemoved.indexOf('pending_deletion_.reset()');
    const corePost = onProfileWasRemoved.indexOf('maho::PostCoreTask');
    contract(
        /profile_path\s*!=\s*pending_deletion_->profile_path/.test(
            onProfileWasRemoved),
        'OnProfileWasRemoved must require the exact pending Chromium path');
    contract(
        pendingMove >= 0 && pendingClear > pendingMove && corePost > pendingClear &&
            /base::BindOnce\(\s*&DeleteProfileOnCoreSequence\s*,\s*pending\.canonical_profile_id\s*\)/.test(
                onProfileWasRemoved) &&
            /base::BindOnce\(\s*&ProfileDeletionCoordinator::OnCoreDeletionFinished\s*,[\s\S]*std::move\(pending\)\s*\)/.test(
                onProfileWasRemoved),
        'matching removal must move and clear pending state reentrancy-safely before posting core deletion to coordinator completion');
    contract(
        !/DeleteProfileOnCoreSequence/.test(coordinatorDelete),
        'core deletion must not begin before matching ProfileAttributesStorage removal');

    contract(
        /ReconcileProfileRegistryFromCore\(\)/.test(onCoreDeletionFinished) &&
            /record_absent\s*=\s*!bridge->CanonicalizeProfileId\(/.test(
                onCoreDeletionFinished) &&
            /core_success\s*\|\|\s*core_not_found/.test(
                onCoreDeletionFinished),
        'core completion must reconcile and require record absence plus a successful or already-absent core result');
    contract(
        /if\s*\(!success\)[\s\S]*SetProfileLifecycleState\([\s\S]*kRepairRequired/.test(
            onCoreDeletionFinished) &&
            /NotifyChanged\(\/\*is_structural=\*\/true\)/.test(
                onCoreDeletionFinished) &&
            /std::move\(pending\.callback\)\.Run\(success\)/.test(
                onCoreDeletionFinished),
        'core completion failure must mark repair required, structurally notify, and complete with the reconciled result');

    contract(
        /profile_attributes_observation_\.Reset\(\)/.test(
            onProfileManagerDestroying) &&
            /profile_manager_observation_\.Reset\(\)/.test(
                onProfileManagerDestroying) &&
            /PendingDeletion pending = std::move\(\*pending_deletion_\)[\s\S]*pending_deletion_\.reset\(\)[\s\S]*std::move\(pending\.callback\)\.Run\(false\)/.test(
                onProfileManagerDestroying),
        'ProfileManager destruction must reset observations and move-clear-run the pending callback false exactly once');
    contract(
        /PendingDeletion pending = std::move\(\*pending_deletion_\)[\s\S]*pending_deletion_\.reset\(\)[\s\S]*std::move\(pending\.callback\)\.Run\(false\)/.test(
            coordinatorDestructor),
        'coordinator destruction must move-clear-run any pending callback false exactly once');
    contract(
        !/\bOnProfileDeleted\b|\bOnChromiumProfileDeletionAccepted\b/.test(
            handlerHeader) &&
            !/MahoSettingsPageHandler::(?:OnProfileDeleted|OnChromiumProfileDeletionAccepted)\b/.test(
                handlerImplementation),
        'obsolete handler deletion-completion declarations and definitions must remain absent');

    expect(
        failures,
        'profile deletion must be owned process-wide and completed only after matching ProfileAttributesStorage removal and core reconciliation')
        .toEqual([]);
  });
});
