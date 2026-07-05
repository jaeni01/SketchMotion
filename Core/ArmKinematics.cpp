#include "ArmKinematics.h"
#include <cmath>

namespace sm {

ArmPose ForwardKinematics(const ArmConfig& cfg, const JointAngles& q) {
    ArmPose pose;
    pose.shoulder = cfg.base;
    pose.elbow = { cfg.base.x + cfg.l1 * std::cos(q.shoulder),
                   cfg.base.y + cfg.l1 * std::sin(q.shoulder) };
    const float t = q.shoulder + q.elbow;
    pose.wrist = { pose.elbow.x + cfg.l2 * std::cos(t),
                   pose.elbow.y + cfg.l2 * std::sin(t) };
    return pose;
}

bool IsReachable(const ArmConfig& cfg, const PointF& target) {
    const float d = Distance(cfg.base, target);
    const float outer = cfg.l1 + cfg.l2;
    const float inner = std::abs(cfg.l1 - cfg.l2);
    return d <= outer && d >= inner;
}

std::optional<JointAngles> InverseKinematics(const ArmConfig& cfg,
                                             const PointF& target,
                                             bool elbowUp) {
    const float dx = target.x - cfg.base.x;
    const float dy = target.y - cfg.base.y;
    const float distSq = dx * dx + dy * dy;

    // 코사인 법칙: cos(elbow) = (d^2 - l1^2 - l2^2) / (2 l1 l2)
    float cosElbow = (distSq - cfg.l1 * cfg.l1 - cfg.l2 * cfg.l2)
                   / (2.0f * cfg.l1 * cfg.l2);
    if (cosElbow > 1.0f + 1e-5f || cosElbow < -1.0f - 1e-5f)
        return std::nullopt; // 도달 불가
    cosElbow = std::clamp(cosElbow, -1.0f, 1.0f);

    const float elbowMag = std::acos(cosElbow);
    const float elbow = elbowUp ? -elbowMag : elbowMag;

    // 어깨각 = 목표 방향각 - 내부 오프셋각
    const float k1 = cfg.l1 + cfg.l2 * std::cos(elbow);
    const float k2 = cfg.l2 * std::sin(elbow);
    const float shoulder = std::atan2(dy, dx) - std::atan2(k2, k1);

    return JointAngles{ shoulder, elbow };
}

} // namespace sm
