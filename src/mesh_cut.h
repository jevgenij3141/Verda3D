#pragma once

#include "scene.h"

enum class CutResult { Success, OpenMesh, NoOverlap, Empty, TooComplex, Unstable };
CutResult subtractMesh(const Model& target, const Model& cutter, Mesh& result);
