#pragma once
// ArmKinematics.h - 2링크 평면 로봇팔 순기구학/역기구학
#include <optional>
#include "Geometry.h"

namespace sm {

struct ArmConfig {
    PointF base{ 0.0f, 0.0f }; // 어깨 관절 위치
    float l1 = 220.0f;         // 상완 길이
    float l2 = 180.0f;         // 하완 길이
};

struct JointAngles {
    float shoulder = 0.0f; // 라디안, x축 기준 반시계
    float elbow = 0.0f;    // 라디안, 상완 기준 상대 각
};

struct ArmPose {
    PointF shoulder; // = config.base
    PointF elbow;    // 팔꿈치 관절 위치
    PointF wrist;    // 엔드이펙터(펜) 위치
};

// 순기구학: 관절각 -> 링크 끝 위치들
ArmPose ForwardKinematics(const ArmConfig& cfg, const JointAngles& q);

// 역기구학: 목표점 -> 관절각. 도달 불가능하면 nullopt.
// elbowUp: 팔꿈치 굽힘 방향 선택 (해가 2개인 경우)
std::optional<JointAngles> InverseKinematics(const ArmConfig& cfg,
                                             const PointF& target,
                                             bool elbowUp = true);

// 목표점이 작업 영역(도넛) 안에 있는가
bool IsReachable(const ArmConfig& cfg, const PointF& target);

} // namespace sm
