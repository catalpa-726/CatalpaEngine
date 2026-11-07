#include "catpch.h"
#include "Animation/BlendTreeAsset.h"

namespace Catalpa {
    bool ValidateBlendTree(const BlendTreeAsset& tree, std::string& error) try {
        if (!tree.skeleton.HasValue() || tree.nodes.empty() || tree.nodes.size() > 256 ||
            tree.states.empty() || tree.states.size() > 128 || tree.transitions.size() > 512 ||
            tree.parameters.size() > 128 ||
            (tree.skeletonVersion.HasValue() && tree.skeletonVersion.GetId() != tree.skeleton)) {
            error = "Blend tree identity or graph budget is invalid";
            return false;
        }
        std::map<StableId, const BlendTreeNode*> nodes;
        for (const auto& [name, value] : tree.parameters)
            if (name.empty() || name.size() > 128 || name.find('\0') != name.npos || !std::isfinite(value)) {
                error = "Blend parameter is invalid";
                return false;
            }
        std::optional<StableId> rootMotion;
        for (const auto& node : tree.nodes) {
            if (!node.id.HasValue() || !nodes.emplace(node.id, &node).second || !std::isfinite(node.speed) ||
                node.speed < -8 || node.speed > 8) {
                error = "Blend node identity or speed is invalid";
                return false;
            }
            if (node.kind == BlendNodeKind::Clip) {
                if (!node.clip.HasValue() || !node.children.empty() || !node.thresholds.empty()) {
                    error = "Clip node cannot have blend children";
                    return false;
                }
                if (node.clipVersion.HasValue()) {
                    const auto& clip = *node.clipVersion.Get();
                    if (node.clipVersion.GetId() != node.clip || clip.skeleton != tree.skeleton ||
                        !clip.skeletonVersion.HasValue() ||
                        (tree.skeletonVersion.HasValue() &&
                         (clip.skeletonVersion.GetVersion() != tree.skeletonVersion.GetVersion() ||
                          clip.skeletonVersion.Get() != tree.skeletonVersion.Get()))) {
                        error = "Blend clips must bind the same skeleton identity and content version";
                        return false;
                    }
                    if (!rootMotion)
                        rootMotion = clip.rootMotionJoint;
                    else if (*rootMotion != clip.rootMotionJoint) {
                        error = "Blended clips must agree on the root-motion joint";
                        return false;
                    }
                }
            } else if (node.kind == BlendNodeKind::Blend1D) {
                if (node.speed != 1 || node.clip.HasValue() || node.children.size() < 2 ||
                    node.children.size() > 32 || node.children.size() != node.thresholds.size() ||
                    !tree.parameters.contains(node.parameter)) {
                    error = "Blend1D requires unit node speed, a parameter and 2..32 child/threshold pairs; "
                            "playback speed belongs to clip nodes";
                    return false;
                }
                for (size_t i = 0; i < node.thresholds.size(); ++i)
                    if (!std::isfinite(node.thresholds[i]) ||
                        (i && node.thresholds[i] <= node.thresholds[i - 1])) {
                        error = "Blend1D thresholds must strictly increase";
                        return false;
                    }
            } else {
                error = "Unknown blend node kind";
                return false;
            }
        }
        std::map<StableId, uint8_t> colors;
        for (const auto& [id, position] : tree.layout)
            if (!nodes.contains(id) || !std::isfinite(position.first) || !std::isfinite(position.second) ||
                std::abs(position.first) > 100000 || std::abs(position.second) > 100000) {
                error = "Blend canvas position is invalid or refers to a missing node";
                return false;
            }
        std::function<bool(StableId)> visit = [&](StableId id) {
            const auto found = nodes.find(id);
            if (found == nodes.end()) {
                error = "Blend child node is missing: " + id.ToString();
                return false;
            }
            if (colors[id] == 1) {
                error = "Blend node cycle at " + id.ToString();
                return false;
            }
            if (colors[id] == 2)
                return true;
            colors[id] = 1;
            for (auto child : found->second->children)
                if (!visit(child))
                    return false;
            colors[id] = 2;
            return true;
        };
        for (const auto& [id, node] : nodes)
            if (!visit(id))
                return false;
        std::set<StableId> states;
        for (const auto& state : tree.states)
            if (!state.id.HasValue() || !states.insert(state.id).second || !nodes.contains(state.node) ||
                state.name.empty() || state.name.size() > 256 || state.name.find('\0') != state.name.npos) {
                error = "Blend state identity, node or name is invalid";
                return false;
            }
        if (!states.contains(tree.initialState)) {
            error = "Blend initial state is missing";
            return false;
        }
        std::set<std::pair<StableId, StableId>> transitionIds;
        for (const auto& transition : tree.transitions)
            if (!transitionIds.emplace(transition.from, transition.to).second ||
                !states.contains(transition.from) || !states.contains(transition.to) ||
                transition.from == transition.to || !tree.parameters.contains(transition.parameter) ||
                !std::isfinite(transition.threshold) || !std::isfinite(transition.duration) ||
                transition.duration < 0 || transition.duration > 10) {
                error = "Blend transition is invalid or has an ambiguous duplicate from/to pair";
                return false;
            }
        error.clear();
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }

    bool EvaluateBlendTree(const BlendTreeAsset& tree,
                           const std::map<std::string, float, std::less<>>& overrides, double dt,
                           BlendTreeRuntime& state, AnimationAdvance& output, std::string& error,
                           bool extractRootMotion, double playbackSpeed) try {
        if (!std::isfinite(dt) || dt < 0 || dt > 1 || !std::isfinite(playbackSpeed) ||
            std::abs(playbackSpeed) > 8 || !tree.skeletonVersion.HasValue() ||
            !ValidateBlendTree(tree, error)) {
            if (error.empty())
                error = "Blend evaluator requires a fixed skeleton and bounded seconds";
            return false;
        }
        auto parameters = tree.parameters;
        for (const auto& [key, value] : overrides) {
            if (!parameters.contains(key) || !std::isfinite(value)) {
                error = "Unknown or non-finite blend parameter: " + key;
                return false;
            }
            parameters[key] = value;
        }
        BlendTreeRuntime next = state;
        if (!std::isfinite(next.transitionTime) || next.transitionTime < 0 || next.transitionTime > 10) {
            error = "Blend runtime transition time is invalid";
            return false;
        }
        if (!next.state.HasValue())
            next.state = tree.initialState;
        std::map<StableId, const BlendTreeState*> states;
        std::map<StableId, const BlendTreeNode*> nodes;
        for (const auto& node : tree.nodes)
            nodes.emplace(node.id, &node);
        for (const auto& item : tree.states)
            states.emplace(item.id, &item);
        std::erase_if(next.evaluators, [&](const auto& entry) {
            return !nodes.contains(entry.first) || nodes.at(entry.first)->kind != BlendNodeKind::Clip;
        });
        std::erase_if(next.players, [&](const auto& entry) {
            return !nodes.contains(entry.first) || nodes.at(entry.first)->kind != BlendNodeKind::Clip;
        });
        if (!states.contains(next.state) ||
            (next.previousState.HasValue() && !states.contains(next.previousState))) {
            error = "Blend runtime references a removed state";
            return false;
        }
        // 节点拥有独立时钟，未激活分支冻结；共享节点每 tick 最多推进一次。
        if (!next.previousState.HasValue())
            for (const auto& transition : tree.transitions) {
                if (transition.from != next.state)
                    continue;
                const bool condition = parameters.at(transition.parameter) >= transition.threshold;
                if (condition != transition.greaterEqual)
                    continue;
                next.previousState = next.state;
                next.state = transition.to;
                next.transitionTime = 0;
                break;
            }
        std::map<StableId, AnimationAdvance> cache;
        std::function<bool(StableId, AnimationAdvance&)> evaluate = [&](StableId id, AnimationAdvance& out) {
            if (const auto found = cache.find(id); found != cache.end()) {
                out = found->second;
                return true;
            }
            const auto& node = *nodes.at(id);
            AnimationAdvance result;
            if (node.kind == BlendNodeKind::Clip) {
                if (!node.clipVersion.HasValue()) {
                    error = "Blend clip version is unavailable";
                    return false;
                }
                if (!next.evaluators[id].Bind(node.clipVersion, error) ||
                    !next.evaluators[id].Advance(dt, node.speed * playbackSpeed, true, extractRootMotion,
                                                 next.players[id], result, error))
                    return false;
            } else {
                const auto value = parameters.at(node.parameter);
                const auto found = std::upper_bound(node.thresholds.begin(), node.thresholds.end(), value);
                if (found == node.thresholds.begin()) {
                    if (!evaluate(node.children.front(), result))
                        return false;
                } else if (found == node.thresholds.end()) {
                    if (!evaluate(node.children.back(), result))
                        return false;
                } else {
                    const size_t right = size_t(found - node.thresholds.begin()), left = right - 1;
                    // 有限 float 阈值的跨度仍可能超出 float；在 double 中求比例，保留作者阈值原值。
                    const float weight =
                        float((double(value) - double(node.thresholds[left])) /
                              (double(node.thresholds[right]) - double(node.thresholds[left])));
                    AnimationAdvance a, b;
                    if (!evaluate(node.children[left], a) || !evaluate(node.children[right], b) ||
                        !BlendPoses(*tree.skeletonVersion.Get(), a.pose, b.pose, weight, result.pose, error))
                        return false;
                    result.rootTranslation = glm::mix(a.rootTranslation, b.rootTranslation, weight);
                    result.rootRotation = glm::normalize(glm::slerp(a.rootRotation, b.rootRotation, weight));
                    // 连续量混合；离散事件只交付权重较大的分支，平分时右侧获选。
                    result.events = weight >= .5f ? std::move(b.events) : std::move(a.events);
                }
            }
            cache.emplace(id, result);
            out = std::move(result);
            return true;
        };
        AnimationAdvance current;
        if (!evaluate(states.at(next.state)->node, current))
            return false;
        if (next.previousState.HasValue()) {
            const auto transition =
                std::find_if(tree.transitions.begin(), tree.transitions.end(), [&](const auto& t) {
                    return t.from == next.previousState && t.to == next.state;
                });
            if (transition == tree.transitions.end()) {
                error = "Active blend transition no longer exists";
                return false;
            }
            next.transitionTime = std::min(next.transitionTime + dt, transition->duration);
            const float weight =
                transition->duration == 0 ? 1.0f : float(next.transitionTime / transition->duration);
            if (weight < 1) {
                AnimationAdvance before;
                AnimationAdvance blended;
                if (!evaluate(states.at(next.previousState)->node, before) ||
                    !BlendPoses(*tree.skeletonVersion.Get(), before.pose, current.pose, weight, blended.pose,
                                error))
                    return false;
                blended.rootTranslation = glm::mix(before.rootTranslation, current.rootTranslation, weight);
                blended.rootRotation =
                    glm::normalize(glm::slerp(before.rootRotation, current.rootRotation, weight));
                blended.events = weight >= .5f ? std::move(current.events) : std::move(before.events);
                current = std::move(blended);
            } else
                next.previousState = {};
        }
        state = std::move(next);
        output = std::move(current);
        error.clear();
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
} // namespace Catalpa
