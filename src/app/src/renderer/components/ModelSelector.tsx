import React, { useState, useRef, useEffect } from 'react';
import { useModels, DEFAULT_MODEL_ID } from '../hooks/useModels';
import { isCollectionModel } from '../utils/collectionModels';

interface ModelSelectorProps {
  disabled: boolean;
}

// Strip the "user.halo-" / "user." namespace from custom-registered models so
// the dropdown shows the bare model name. user.halo-1bit-2b → 1bit-2b,
// user.bonsai-1.7b-tq2-h1b → bonsai-1.7b-tq2-h1b. The full id (with prefix)
// stays in titles/tooltips and is what the API sees — only the visible label
// is shortened.
const displayId = (id: string): string =>
  id.replace(/^user\.halo-/, '').replace(/^user\./, '');

const ModelSelector: React.FC<ModelSelectorProps> = ({ disabled }) => {
  const {
    downloadedModels,
    selectedModel,
    setSelectedModel,
    isDefaultModelPending,
    setUserHasSelectedModel,
  } = useModels();

  const [isOpen, setIsOpen] = useState(false);
  const [searchQuery, setSearchQuery] = useState('');
  const containerRef = useRef<HTMLDivElement>(null);
  const searchRef = useRef<HTMLInputElement>(null);

  const visibleDownloadedModels = downloadedModels.filter((model) => {
    if (model.info?.labels?.includes('esrgan')) return false;
    if (!isCollectionModel(model.info)) {
      return true;
    }
    return model.info.suggested === true;
  });

  const allModels = isDefaultModelPending
    ? [{ id: DEFAULT_MODEL_ID }]
    : visibleDownloadedModels;

  const dropdownModels = searchQuery.trim()
    ? allModels.filter(m => m.id.toLowerCase().includes(searchQuery.toLowerCase()))
    : allModels;

  useEffect(() => {
    const handleClickOutside = (e: MouseEvent) => {
      if (containerRef.current && !containerRef.current.contains(e.target as Node)) {
        setIsOpen(false);
      }
    };
    document.addEventListener('mousedown', handleClickOutside);
    return () => document.removeEventListener('mousedown', handleClickOutside);
  }, []);

  useEffect(() => {
    if (isOpen) {
      setSearchQuery('');
      setTimeout(() => searchRef.current?.focus(), 0);
    }
  }, [isOpen]);

  const handleSelect = (id: string) => {
    setUserHasSelectedModel(true);
    setSelectedModel(id);
    setIsOpen(false);
  };

  return (
    <div
      ref={containerRef}
      className={`model-selector-custom${disabled ? ' disabled' : ''}`}
    >
      <button
        className="model-selector-trigger"
        onClick={() => !disabled && setIsOpen(prev => !prev)}
        disabled={disabled}
        title={selectedModel}
      >
        <span className="model-selector-label">{displayId(selectedModel)}</span>
        <svg className="model-selector-chevron" width="10" height="10" viewBox="0 0 10 10">
          <path d="M2 3.5L5 6.5L8 3.5" stroke="currentColor" strokeWidth="1.5" fill="none" strokeLinecap="round" strokeLinejoin="round"/>
        </svg>
      </button>

      {isOpen && (
        <div className="model-selector-dropdown">
          <div className="model-selector-list">
            {dropdownModels.length > 0 ? dropdownModels.map((model) => (
              <div
                key={model.id}
                className={`model-selector-option${model.id === selectedModel ? ' selected' : ''}`}
                onClick={() => handleSelect(model.id)}
                title={model.id}
              >
                {displayId(model.id)}
              </div>
            )) : (
              <div className="model-selector-empty">No models match</div>
            )}
          </div>
          <div className="model-selector-search-bar">
            <input
              ref={searchRef}
              type="text"
              className="model-selector-search"
              placeholder="Search models..."
              value={searchQuery}
              onChange={(e) => setSearchQuery(e.target.value)}
              onKeyDown={(e) => {
                if (e.key === 'Escape') { e.stopPropagation(); setIsOpen(false); }
              }}
            />
          </div>
        </div>
      )}
    </div>
  );
};

export default ModelSelector;
