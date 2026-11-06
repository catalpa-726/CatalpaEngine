#include "AssetEditors/BehaviorGraphEditor.h"
#include "Commands/GraphCommands.h"
#include "BehaviorGraph/BehaviorGraphCompiler.h"
#include "Core/Application.h"
#include "Scripting/BehaviorGraphRuntime.h"
#include "imgui.h"

namespace Catalpa::Editor {
    namespace {
        const BehaviorNodeDescription& Kind(const BehaviorNode& node) {
            return *std::find_if(BehaviorNodeTypes().begin(),BehaviorNodeTypes().end(),[&](const auto& n){return n.name==node.type;});
        }
        bool Commit(AssetDocument& doc,const BehaviorGraphAsset& graph,std::string& error) {
            std::string text;
            return WriteBehaviorGraph(graph,text,error) && (text==*doc.Source() || ChangeAuthorSource(doc,std::move(text),error));
        }
    }

    void BehaviorGraphEditor::EditToolbar(AssetDocument& doc, BehaviorGraphAsset& graph, std::string& error) {
        if(ImGui::BeginCombo("Add behavior node","Choose node type")) {
            for(const auto& type:BehaviorNodeTypes()) if(ImGui::Selectable(std::string(type.name).c_str())) {
                auto next=graph;BehaviorNode node;
                if(!GenerateStableId(node.id,error)) continue;
                node.type=type.name;node.x=float(next.nodes.size()%4)*220;node.y=float(next.nodes.size()/4)*180;
                if(node.type=="Event" || node.type=="EmitEvent") node.event="event_"+node.id.ToString().substr(0,8);
                if(node.type=="SetNumber" || node.type=="AddNumber" || node.type=="Condition") {
                    if(next.variables.empty()) next.variables.emplace("value",0);
                    node.variable=next.variables.begin()->first;
                }
                next.nodes.push_back(node);
                if(Commit(doc,next,error)) {graph=std::move(next);m_Selected=node.id;}
            }
            ImGui::EndCombo();
        }
        ImGui::InputText("Variable name",m_Variable.data(),m_Variable.size());
        if(ImGui::Button("Add numeric variable") && m_Variable[0]) {
            auto next=graph;
            if(!next.variables.emplace(m_Variable.data(),0).second) error="Behavior variable already exists";
            else if(Commit(doc,next,error)) graph=std::move(next);
        }
    }

    void BehaviorGraphEditor::EditCanvas(AssetDocument& doc, BehaviorGraphAsset& graph, std::string& error) {
        std::vector<CanvasNode> nodes;
        std::vector<CanvasLink> links;
        std::map<StableId,std::pair<StableId,std::string>> outputs;
        for(const auto& node:graph.nodes) {
            CanvasNode n; n.id=node.id;n.title=node.type;n.x=node.x;n.y=node.y;
            const auto& kind=Kind(node);
            if(!kind.entry) n.pins.push_back({CanvasIdentity(node.id,"in"),node.id,"in",true});
            for(const auto& port:kind.outputs) {
                const auto name=std::string(port);
                const auto id=CanvasIdentity(node.id,"out/"+name);
                n.pins.push_back({id,node.id,name,false});outputs[id]={node.id,name};
                if(const auto target=node.flow.find(port);target!=node.flow.end())
                    links.push_back({CanvasIdentity(node.id,"link/"+name),id,CanvasIdentity(target->second,"in")});
            }
            nodes.push_back(std::move(n));
        }
        const auto check=[&](const CanvasPin& from,const CanvasPin& to,std::string& diagnostic) {
            const auto found=outputs.find(from.id);
            if(found==outputs.end() || from.node==to.node) {diagnostic="Use a control output and another action node";return false;}
            auto next=graph;
            const auto node=std::find_if(next.nodes.begin(),next.nodes.end(),[&](const auto& n){return n.id==from.node;});
            node->flow[found->second.second]=to.node;
            return ValidateBehaviorGraph(next,diagnostic);
        };
        const auto changes=m_Canvas.Draw(doc.Id(),nodes,links,check,error);
        if(changes.selected.HasValue()) m_Selected=changes.selected;
        auto next=graph;
        if(changes.connection) {
            const auto& [from,to]=*changes.connection;
            const auto node=std::find_if(next.nodes.begin(),next.nodes.end(),[&](const auto& n){return n.id==from.node;});
            node->flow[outputs.at(from.id).second]=to.node;
        }
        for(auto id:changes.removeLinks) for(auto& node:next.nodes)
            std::erase_if(node.flow,[&](const auto& p){return CanvasIdentity(node.id,"link/"+p.first)==id;});
        for(auto id:changes.removeNodes) {
            std::erase_if(next.nodes,[&](const auto& n){return n.id==id;});
            for(auto& node:next.nodes) std::erase_if(node.flow,[&](const auto& p){return p.second==id;});
        }
        for(const auto& [id,p]:changes.positions) {
            const auto n=std::find_if(next.nodes.begin(),next.nodes.end(),[&](const auto& node){return node.id==id;});
            if(n!=next.nodes.end()) {n->x=p.first;n->y=p.second;}
        }
        if(changes.connection || !changes.removeLinks.empty() || !changes.removeNodes.empty() || !changes.positions.empty())
            if(Commit(doc,next,error)) graph=std::move(next);
    }

    void BehaviorGraphEditor::EditProperties(AssetDocument& doc, BehaviorGraphAsset& graph, std::string& error) {
        if(auto node=std::find_if(graph.nodes.begin(),graph.nodes.end(),[&](const auto& n){return n.id==m_Selected;});node!=graph.nodes.end()) {
            ImGui::Text("Node: %s",node->id.ToString().c_str());
            bool changed=ImGui::InputDouble("Number / wait seconds / comparison",&node->number,0,0,"%.6g",ImGuiInputTextFlags_EnterReturnsTrue);
            changed|=ImGui::InputFloat3("Translation delta (m)",&node->vector.x,"%.6g",ImGuiInputTextFlags_EnterReturnsTrue);
            if(node->type=="Condition" || node->type=="SetNumber" || node->type=="AddNumber") {
                if(ImGui::BeginCombo("Variable",node->variable.c_str())) {
                    for(const auto& [name,value]:graph.variables) if(ImGui::Selectable(name.c_str())) {node->variable=name;changed=true;}
                    ImGui::EndCombo();
                }
            }
            if(node->type=="Event" || node->type=="EmitEvent") {
                std::array<char,129> name{};
                std::copy_n(node->event.data(),std::min(node->event.size(),name.size()-1),name.data());
                if(ImGui::InputText("Event name",name.data(),name.size(),ImGuiInputTextFlags_EnterReturnsTrue)) {node->event=name.data();changed=true;}
            }
            if(changed) Commit(doc,graph,error);
        }
        if(ImGui::CollapsingHeader("Initial variables")) for(auto& [name,value]:graph.variables)
            if(ImGui::InputDouble(name.c_str(),&value,0,0,"%.6g",ImGuiInputTextFlags_EnterReturnsTrue)) Commit(doc,graph,error);
    }

    void BehaviorGraphEditor::DrawDebug(Application& app, const AssetDocument& doc, std::string& error) {
        if(ImGui::CollapsingHeader("Play execution snapshots")) {
            ImGui::InputText("Emit event",m_Event.data(),m_Event.size());
            for(const auto& snapshot:app.GetBehaviorDebug()) {
                if (snapshot.graph != doc.Asset()) continue;
                ImGui::PushID(snapshot.entity.ToString().c_str());
                ImGui::Text("Entity: %s / node: %s",snapshot.entity.ToString().c_str(),snapshot.node.ToString().c_str());
                ImGui::Text("Running program version: %llu (author draft is separate)",
                            static_cast<unsigned long long>(snapshot.version));
                ImGui::TextWrapped("%s",snapshot.error.c_str());
                if (snapshot.node.HasValue() && ImGui::Button("Locate running node")) {
                    m_Selected = snapshot.node;
                    m_Canvas.Focus(snapshot.node);
                }
                for(const auto& [name,value]:snapshot.variables) ImGui::Text("%s = %.8g",name.c_str(),value);
                if(ImGui::Button("Send to this running instance")) app.EmitBehaviorEvent(snapshot.entity,m_Event.data(),error);
                ImGui::PopID();
            }
        }
    }

    void BehaviorGraphEditor::Draw(Application& app, AssetDocument& doc, const PanelText&, std::string& error) {
        BehaviorGraphAsset graph;
        std::string validation;
        if (!Tools::BehaviorGraphCompiler::Compile(*doc.Source(), graph, validation)) {
            error = std::move(validation);
            return;
        }
        EditToolbar(doc, graph, error);
        EditCanvas(doc, graph, error);
        EditProperties(doc, graph, error);
        DrawDebug(app, doc, error);
    }
}
