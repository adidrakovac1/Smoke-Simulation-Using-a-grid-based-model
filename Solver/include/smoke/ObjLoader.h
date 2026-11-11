#pragma once

#include "smoke/Obstacles.h"

#include <iosfwd>
#include <string>

namespace smoke {

/// Reads the geometry of a Wavefront .obj file: "v x y z" lines and "f ..." lines. Faces with more than three
/// corners are split into a triangle fan; "v/vt/vn" corner forms and negative (relative) indices are accepted.
/// Everything else (normals, texture coordinates, materials, groups) is ignored.
///
/// A small parser of our own instead of a library, so the Solver keeps having no dependencies (decision D16).
/// Returns false and fills `error` (with the line number) when the text is not usable.
bool parseObj(std::istream& input, TriangleMesh& out, std::string* error = nullptr);

/// parseObj for a file on disk.
bool loadObj(const std::string& path, TriangleMesh& out, std::string* error = nullptr);

} // namespace smoke
