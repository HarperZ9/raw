#pragma once
// The engine's GTAO (native/ post_gtao, WGSL and its CPU reference) written once more with
// the floating-point type of each stage as a parameter. With every stage in float it computes
// what the GPU pass computes; with every stage in double it is raw::post::gtao. Switching one
// stage at a time shows which stage makes the GPU differ from the reference.
//
//   D  the slice's screen direction: project(P) and project(P + dir * 1e-3 * depth)
//   C  the sample's pixel coordinates and the bilinear depth lookup
//   R  everything else: the basis, the horizon angles, the integral
//
// Test code only. Original work, MIT.
#include "raw/renderer/post.hpp"
#include <vector>

namespace precision
{
// stages: three letters, 'f' or 'd', for D, C and R. "fff" is the GPU's arithmetic, "ddd" the reference's.
// "aff" is all float with the slice direction taken from the projection's derivative.
std::vector<float> Variant(const raw::post::ViewGBuffer& v, double radius, int slices, int steps, const char* stages);
// Fraction of drawn pixels where two results differ by more than 1e-3.
double Outside(const std::vector<float>& a, const std::vector<float>& b, const raw::post::ViewGBuffer& v);
} // namespace precision
