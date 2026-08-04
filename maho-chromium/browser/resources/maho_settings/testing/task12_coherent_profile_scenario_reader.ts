import type {
  ProfileInfo,
  ProfileTarget,
  SelectedProfileContext,
} from '../maho_settings.mojom-webui.js';
import type {ProfileScenarioSnapshot} from './task12_profile_scenarios.js';

export interface CapturedSelectedProfileStoreState {
  readonly selectedProfileTarget: ProfileTarget | null;
  readonly selectedProfileContext: SelectedProfileContext | null;
  readonly selectedProfileLoading: boolean;
  readonly selectedProfileError: unknown | null;
}

export interface ProfileObservablesDto {
  readonly activeBrowserProfileId: string | null;
  readonly activeMahoProfileId: string | null;
  readonly registryRevision: bigint | null;
  readonly lifecycleEntries: readonly {
    readonly profileId: string;
    readonly lifecycleState: number;
  }[];
}

export interface ProfileCatalogDto {
  readonly profiles: readonly ProfileInfo[];
}

export interface CoherentProfileScenarioReaderDependencies {
  awaitSelectedStoreState(): Promise<CapturedSelectedProfileStoreState>;
  readObservables(): Promise<ProfileObservablesDto>;
  listProfiles(): Promise<ProfileCatalogDto>;
  revalidateSelectedTarget(target: ProfileTarget): Promise<SelectedProfileContext | null>;
  readLiveStoreState(): CapturedSelectedProfileStoreState |
      Promise<CapturedSelectedProfileStoreState>;
}

export class CoherentProfileScenarioSnapshotUnavailableError extends Error {}

export async function readCoherentProfileScenarioSnapshot(
    dependencies: CoherentProfileScenarioReaderDependencies,
): Promise<ProfileScenarioSnapshot> {
  for (let attempt = 0; attempt < 2; ++attempt) {
    try {
      const captured = await dependencies.awaitSelectedStoreState();
      const target = captured.selectedProfileTarget;
      const context = captured.selectedProfileContext;
      if (target === null || context === null ||
          target.profileId !== context.profileId ||
          target.targetToken !== context.targetToken ||
          context.targetToken.length === 0 ||
          captured.selectedProfileLoading || captured.selectedProfileError !== null) {
        continue;
      }

      const observablesBefore = await dependencies.readObservables();
      const catalog = await dependencies.listProfiles();
      const revalidated = await dependencies.revalidateSelectedTarget(target);
      const observablesAfter = await dependencies.readObservables();
      const live = await dependencies.readLiveStoreState();

      if (observablesBefore.registryRevision === null ||
          observablesAfter.registryRevision === null ||
          observablesBefore.registryRevision !== observablesAfter.registryRevision ||
          revalidated === null ||
          revalidated.profileId !== context.profileId ||
          revalidated.targetToken !== context.targetToken ||
          revalidated.contextRevision !== context.contextRevision ||
          revalidated.profileRevision !== context.profileRevision ||
          live.selectedProfileTarget?.profileId !== target.profileId ||
          live.selectedProfileTarget.targetToken !== target.targetToken ||
          live.selectedProfileContext?.profileId !== context.profileId ||
          live.selectedProfileContext.targetToken !== context.targetToken ||
          live.selectedProfileContext.contextRevision !== context.contextRevision ||
          live.selectedProfileContext.profileRevision !== context.profileRevision ||
          live.selectedProfileLoading !== captured.selectedProfileLoading ||
          live.selectedProfileError !== captured.selectedProfileError) {
        continue;
      }

      const catalogIds = catalog.profiles.map(profile => profile.id);
      const lifecycleIds = observablesAfter.lifecycleEntries.map(entry => entry.profileId);
      if (new Set(catalogIds).size !== catalogIds.length ||
          new Set(lifecycleIds).size !== lifecycleIds.length ||
          catalogIds.length !== lifecycleIds.length ||
          catalogIds.some(profileId => !lifecycleIds.includes(profileId))) {
        continue;
      }

      const selectedLifecycle = observablesAfter.lifecycleEntries.find(
          entry => entry.profileId === context.profileId);
      if (selectedLifecycle === undefined) {
        continue;
      }

      return {
        profiles: catalog.profiles.map(profile => ({id: profile.id, name: profile.name})),
        selectedProfileId: context.profileId,
        targetToken: context.targetToken,
        contextRevision: context.contextRevision,
        profileRevision: context.profileRevision,
        ready: selectedLifecycle.lifecycleState === 1,
      };
    } catch {
      // Retry once, then expose only the stable fail-closed error below.
    }
  }

  throw new CoherentProfileScenarioSnapshotUnavailableError(
      'Coherent selected profile scenario snapshot is unavailable or stale.');
}
