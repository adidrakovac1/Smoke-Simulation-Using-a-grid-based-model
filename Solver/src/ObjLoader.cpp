#include "smoke/ObjLoader.h"

#include <cstdlib>
#include <fstream>
#include <istream>
#include <sstream>
#include <vector>

namespace smoke {

namespace {

void setError(std::string* error, int line, const std::string& text) {
    if (error) *error = "line " + std::to_string(line) + ": " + text;
}

} // namespace

bool parseObj(std::istream& input, TriangleMesh& out, std::string* error) {
    out.positions.clear();
    out.indices.clear();

    std::string                line;
    std::vector<std::uint32_t> corners;
    int                        lineNumber = 0;
    while (std::getline(input, line)) {
        ++lineNumber;
        std::istringstream words(line);
        std::string        keyword;
        if (!(words >> keyword)) continue;  // empty line

        if (keyword == "v") {
            float x = 0.0f, y = 0.0f, z = 0.0f;
            if (!(words >> x >> y >> z)) {
                setError(error, lineNumber, "a vertex needs three numbers");
                return false;
            }
            out.positions.insert(out.positions.end(), {x, y, z});
        } else if (keyword == "f") {
            // Each corner is "v", "v/vt", "v//vn" or "v/vt/vn"; only the first number (the vertex) matters here.
            corners.clear();
            std::string corner;
            while (words >> corner) {
                char*      end   = nullptr;
                const long index = std::strtol(corner.c_str(), &end, 10);
                if (end == corner.c_str() || index == 0) {
                    setError(error, lineNumber, "bad face corner '" + corner + "'");
                    return false;
                }
                // Positive indices count from 1; negative ones count back from the last vertex read so far.
                const long vertexCount = static_cast<long>(out.vertexCount());
                const long zeroBased   = index > 0 ? index - 1 : vertexCount + index;
                if (zeroBased < 0 || zeroBased >= vertexCount) {
                    setError(error, lineNumber, "face refers to vertex " + std::to_string(index) + ", which does not exist");
                    return false;
                }
                corners.push_back(static_cast<std::uint32_t>(zeroBased));
            }
            if (corners.size() < 3) {
                setError(error, lineNumber, "a face needs at least three corners");
                return false;
            }
            // Triangle fan around the first corner: (0,1,2), (0,2,3), ... Correct for convex polygons.
            for (std::size_t c = 1; c + 1 < corners.size(); ++c)
                out.indices.insert(out.indices.end(), {corners[0], corners[c], corners[c + 1]});
        }
        // Any other keyword (vn, vt, o, g, s, usemtl, mtllib, #...) carries nothing we need.
    }

    if (out.indices.empty()) {
        setError(error, lineNumber, "no faces found");
        return false;
    }
    return true;
}

bool loadObj(const std::string& path, TriangleMesh& out, std::string* error) {
    std::ifstream file(path);
    if (!file) {
        if (error) *error = "cannot open '" + path + "'";
        return false;
    }
    return parseObj(file, out, error);
}

} // namespace smoke
