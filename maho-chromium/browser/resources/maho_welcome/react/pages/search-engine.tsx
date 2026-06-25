import type {MahoWelcomeStore} from '../store.js';
import type {SearchEngineInfo} from '../../maho_welcome.mojom-webui.js';

interface Props {
  engines: readonly SearchEngineInfo[];
  selectedEngine: string | null;
  onSelect: (keyword: string) => void;
}

export function SearchEngineSidebar({store}: {store: MahoWelcomeStore}) {
  return (
    <>
      <h1>{store.getString('IDS_MAHO_WELCOME_SEARCH_TITLE')}</h1>
      <p>{store.getString('IDS_MAHO_WELCOME_SEARCH_BODY')}</p>
    </>
  );
}

export function SearchEngineContent({engines, selectedEngine, onSelect, store}: Props & {store: MahoWelcomeStore}) {
  const filteredEngines = engines.filter(engine => {
    const name = engine.name?.toLowerCase() ?? '';
    return !name.includes('wikipedia') &&
        !name.includes('ebay');
  });

  if (engines.length === 0) {
    return (
      <div className="welcome-engines welcome-engines--empty">
        <p>Loading search engines…</p>
      </div>
    );
  }

  if (filteredEngines.length === 0) {
    return (
      <div className="welcome-engines welcome-engines--empty">
        <p>No search engines available.</p>
      </div>
    );
  }

  return (
    <div className="welcome-engines">
      {filteredEngines.map(engine => {
        let engineName = engine.name ?? '';
        const keyword = engine.keyword?.toLowerCase() ?? '';
        const trimmedName = engineName.trim();
        const isKo = store.getString('IDS_MAHO_WELCOME_CTA_NEXT') === '다음';

        if (isKo) {
          if (keyword.includes('naver') || trimmedName === 'Naver') {
            engineName = '네이버';
          } else if (keyword.includes('daum') || trimmedName === 'Daum') {
            engineName = '다음';
          }
        } else {
          if (keyword.includes('naver') || trimmedName === '네이버' || trimmedName.includes('네이버')) {
            engineName = 'Naver';
          } else if (keyword.includes('daum') || trimmedName === '다음' || trimmedName.includes('다음')) {
            engineName = 'Daum';
          }
        }

        return (
          <button
            key={engine.keyword}
            type="button"
            className={`welcome-engine-card${engine.keyword === selectedEngine ? ' is-selected' : ''}`}
            aria-pressed={engine.keyword === selectedEngine}
            onClick={() => onSelect(engine.keyword)}>
            {engine.iconUrl ? (
              <img
                className="welcome-engine-card__icon"
                src={engine.iconUrl}
                alt=""
                width={32}
                height={32}
              />
            ) : (
              <div className="welcome-engine-card__icon welcome-engine-card__icon--placeholder">
                {engineName.charAt(0)}
              </div>
            )}
            <span className="welcome-engine-card__name">{engineName}</span>
          </button>
        );
      })}
    </div>
  );
}
