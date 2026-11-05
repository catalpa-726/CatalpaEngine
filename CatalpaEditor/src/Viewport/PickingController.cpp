#include "Viewport/PickingController.h"
#include "Core/Application.h"

namespace Catalpa::Editor {
    void PickingController::Cancel(Application& app) {
        if (m_Ticket)
            app.CancelEditorPick(m_Ticket);
        m_Ticket = 0;
    }

    bool PickingController::Request(Application& app, SceneDocument& doc, const RenderSurfaceSnapshot& frame,
                                    uint32_t x, uint32_t y, std::string& error) {
        auto* scene = doc.Resolve();
        if (!scene || scene->GetWorld().GetIdentity() != frame.sceneIdentity || x >= frame.extent.width ||
            y >= frame.extent.height || m_Request == UINT64_MAX) {
            error = "Selection request does not match the displayed viewport";
            return false;
        }
        Cancel(app);
        PickingRequest request{++m_Request,
                               frame.surfaceId,
                               frame.sceneIdentity,
                               frame.frameSequence,
                               frame.targetGeneration,
                               x,
                               y};
        if (!app.RequestEditorPick(request, m_Ticket, error))
            return false;
        m_Scene = doc.Handle();
        m_Document = doc.Id();
        m_Expected = request;
        return true;
    }

    void PickingController::Update(Application& app, SceneDocument* doc, const RenderSurfaceSnapshot* frame,
                                   EditorSelection& selection, std::string& error) {
        if (!m_Ticket)
            return;
        if (!doc || doc->Id() != m_Document || doc->Handle() != m_Scene || !frame ||
            frame->targetGeneration != m_Expected.targetGeneration ||
            frame->surfaceId != m_Expected.surfaceId || frame->sceneIdentity != m_Expected.sceneIdentity) {
            Cancel(app);
            return;
        }
        PickingResult result;
        if (!app.PollEditorPick(m_Ticket, result))
            return;
        const auto consumed = std::exchange(m_Ticket, 0);
        if (result.ticket != consumed || result.request.requestId != m_Request ||
            result.request.frameSequence != m_Expected.frameSequence || result.request.x != m_Expected.x ||
            result.request.y != m_Expected.y || result.request.targetGeneration != frame->targetGeneration ||
            result.request.sceneIdentity != m_Expected.sceneIdentity ||
            result.request.surfaceId != frame->surfaceId)
            return;
        if (result.status == PickingResultStatus::Failed) {
            error = "GPU picking readback failed";
            return;
        }
        if (result.status != PickingResultStatus::Ready)
            return;
        if (!result.hit.HasValue()) {
            selection.Clear();
            return;
        }
        auto* scene = doc->Resolve();
        EntityId id{result.hit.scene, result.hit.index, result.hit.generation};
        if (scene && scene->GetWorld().IsAlive(id))
            selection.Select(*doc, id);
    }
}
