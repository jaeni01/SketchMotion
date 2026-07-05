#pragma once
// Filters.h - 영상처리 필터 (직접 구현, MFC/OpenCV 비의존)
#include "ImageBuffer.h"

namespace sm {

// BGRA -> 그레이스케일 (ITU-R BT.601 가중치)
GrayImage ToGrayscale(const BgraImage& src);

// 그레이스케일 -> BGRA (프리뷰 표시용)
BgraImage ToBgra(const GrayImage& src);

// 5x5 가우시안 블러 (분리형 커널 1-4-6-4-1)
GrayImage GaussianBlur5(const GrayImage& src);

// Sobel 에지 강도. 결과는 0~255로 클램프된 그래디언트 크기.
GrayImage SobelMagnitude(const GrayImage& src);

// 고정 임계 이진화: v >= threshold ? 255 : 0
GrayImage Threshold(const GrayImage& src, uint8_t threshold);

// Otsu 자동 임계값 계산 (이진화 임계 추천값)
uint8_t OtsuThreshold(const GrayImage& src);

// 명암 반전
GrayImage Invert(const GrayImage& src);

// 타일 평균 기반 적응 이진화 (어두운 픽셀 -> 255).
// 마커 검출용: 조명 불균일에 강함. tile: 타일 한 변(px), bias: 임계 = 타일평균*bias
GrayImage AdaptiveThresholdDark(const GrayImage& src, int tile = 24, float bias = 0.82f);

} // namespace sm
