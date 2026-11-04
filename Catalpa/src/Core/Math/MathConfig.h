#pragma once

// 必须先于任意 GLM 头；避免 DLL、Box 与 Editor 各自选择不一致的数学布局。
#ifdef GLM_SETUP_INCLUDED
#error "Include Core/Math/MathTypes.h before any GLM header."
#endif

#if defined(GLM_FORCE_LEFT_HANDED) || defined(GLM_FORCE_ALIGNED) || defined(GLM_FORCE_ALIGNED_GENTYPES) || defined(GLM_FORCE_DEFAULT_ALIGNED_GENTYPES)
#error "Catalpa math requires right-handed coordinates and packed scalar types."
#endif

#if defined(GLM_FORCE_INTRINSICS) || defined(GLM_FORCE_NEON) || defined(GLM_FORCE_SSE) || defined(GLM_FORCE_SSE2) || defined(GLM_FORCE_SSE3) || defined(GLM_FORCE_SSSE3) || defined(GLM_FORCE_SSE41) || defined(GLM_FORCE_SSE42) || defined(GLM_FORCE_AVX) || defined(GLM_FORCE_AVX2) || defined(GLM_FORCE_AVX512)
#error "Catalpa math selects GLM_FORCE_PURE; per-module SIMD overrides are unsupported."
#endif

#if defined(GLM_FORCE_SINGLE_ONLY) || defined(GLM_FORCE_QUAT_DATA_WXYZ) || defined(GLM_FORCE_QUAT_DATA_XYZW)
#error "Catalpa math requires double intermediates and the default GLM quaternion storage/constructor order."
#endif

#ifndef GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#endif
#ifndef GLM_FORCE_EXPLICIT_CTOR
#define GLM_FORCE_EXPLICIT_CTOR
#endif
#ifndef GLM_FORCE_PURE
#define GLM_FORCE_PURE
#endif
