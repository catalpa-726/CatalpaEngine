#pragma once
#include "AssetEditors/IAssetEditor.h"
#include "Graphs/GraphCanvas.h"
#include "Scripting/BehaviorGraphAsset.h"

namespace Catalpa::Editor {
    class BehaviorGraphEditor final : public IAssetEditor {
      public:
        bool Accepts(const AssetDocument& doc) const override { return doc.TypeKey()=="behavior_graph" && bool(doc.Source()); }
        void Draw(Application&,AssetDocument&,const PanelText&,std::string&) override;
        void Close(Application&) override {m_Canvas.Reset();m_Selected={};}
      private:
        void EditToolbar(AssetDocument&, BehaviorGraphAsset&, std::string& error);
        void EditCanvas(AssetDocument&, BehaviorGraphAsset&, std::string& error);
        void EditProperties(AssetDocument&, BehaviorGraphAsset&, std::string& error);
        void DrawDebug(Application&, const AssetDocument&, std::string& error);
        GraphCanvas m_Canvas;
        StableId m_Selected;
        std::array<char,129> m_Variable{},m_Event{};
    };
}
