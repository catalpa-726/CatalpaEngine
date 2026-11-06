#pragma once
#include "Documents/SceneDocument.h"
#include "Assets/AssetManager.h"

namespace Catalpa::Editor {
    bool InstantiateAuthorPrefab(SceneDocument&,AssetManager&,AssetId,PersistentEntityId parent,
                                 PersistentEntityId& created,std::string& error);
    bool UpdateAuthorPrefab(SceneDocument&,AssetManager&,PersistentEntityId root,bool revert,std::string& error);
}
