#include "GraphObject.h"
#include "../core/Renderer.h"
#include "../core/Camera.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <limits>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>

namespace alice2 {

    GraphVertex::GraphVertex()
        : position(0.0f, 0.0f, 0.0f)
        , color(1.0f, 1.0f, 1.0f) {
    }

    GraphVertex::GraphVertex(const Vec3& pos, const Color& col)
        : position(pos)
        , color(col) {
    }

    GraphEdge::GraphEdge()
        : vertexA(-1)
        , vertexB(-1) {
    }

    GraphEdge::GraphEdge(int a, int b)
        : vertexA(a)
        , vertexB(b) {
    }

    void GraphData::clear() {
        vertices.clear();
        edges.clear();
    }

    int GraphData::addVertex(const Vec3& position, const Color& color) {
        vertices.emplace_back(position, color);
        return static_cast<int>(vertices.size()) - 1;
    }

    int GraphData::addEdge(int vertexA, int vertexB) {
        if (vertexA < 0 || vertexB < 0 ||
            vertexA >= static_cast<int>(vertices.size()) ||
            vertexB >= static_cast<int>(vertices.size())) {
            std::cout << "GraphData::addEdge: Invalid vertex indices (" << vertexA << ", " << vertexB << ")" << std::endl;
            return -1;
        }

        edges.emplace_back(vertexA, vertexB);
        return static_cast<int>(edges.size()) - 1;
    }

    void GraphData::updateBounds(Vec3& minBounds, Vec3& maxBounds) const {
        if (vertices.empty()) {
            minBounds = Vec3(-0.5f, -0.5f, -0.5f);
            maxBounds = Vec3(0.5f, 0.5f, 0.5f);
            return;
        }

        Vec3 minPoint(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
        Vec3 maxPoint(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest());

        for (const auto& vertex : vertices) {
            minPoint.x = std::min(minPoint.x, vertex.position.x);
            minPoint.y = std::min(minPoint.y, vertex.position.y);
            minPoint.z = std::min(minPoint.z, vertex.position.z);

            maxPoint.x = std::max(maxPoint.x, vertex.position.x);
            maxPoint.y = std::max(maxPoint.y, vertex.position.y);
            maxPoint.z = std::max(maxPoint.z, vertex.position.z);
        }

        minBounds = minPoint;
        maxBounds = maxPoint;
    }

    GraphObject::GraphObject(const std::string& name)
        : SceneObject(name)
        , m_graphData(std::make_shared<GraphData>())
        , m_isClosed(false)
        , m_isPolyline(false)
        , m_showVertices(true)
        , m_showEdges(true)
        , m_vertexSize(5.0f)
        , m_edgeWidth(2.0f)
        , m_defaultVertexColor(1.0f, 1.0f, 1.0f)
        , m_edgeColor(0.85f, 0.85f, 0.85f) {
    }

    void GraphObject::setGraphData(std::shared_ptr<GraphData> graphData) {
        m_graphData = std::move(graphData);
        calculateBounds();
        updateTopologyFlags();
    }

    GraphObject GraphObject::duplicate() const {
        GraphObject copy;

        if (m_graphData) {
            copy.setGraphData(std::make_shared<GraphData>(*m_graphData));
        }

        copy.m_showVertices = m_showVertices;
        copy.m_showEdges = m_showEdges;
        copy.m_vertexSize = m_vertexSize;
        copy.m_edgeWidth = m_edgeWidth;
        copy.m_defaultVertexColor = m_defaultVertexColor;
        copy.m_edgeColor = m_edgeColor;
        copy.m_isClosed = m_isClosed;
        copy.m_isPolyline = m_isPolyline;

        return copy;
    }

    float GraphObject::getLength() const {
        float totalLength = 0.0f;
        for(auto edge : m_graphData->edges){
            const Vec3 posA = m_graphData->vertices[edge.vertexA].position;
            const Vec3 posB = m_graphData->vertices[edge.vertexB].position;
            float edgeLength = posA.distanceTo(posB);
            totalLength += edgeLength;
        }
        return totalLength;
    }

    void GraphObject::readFromObj(const std::string& filename) {
        std::ifstream input(filename);
        if (!input) {
            std::cout << "Failed to open OBJ file: " << filename << std::endl;
            return;
        }

        if (!m_graphData) {
            m_graphData = std::make_shared<GraphData>();
        }
        m_graphData->clear();

        std::string line;
        while (std::getline(input, line)) {
            size_t commentPos = line.find('#');
            if (commentPos != std::string::npos) {
                line = line.substr(0, commentPos);
            }

            std::istringstream iss(line);
            std::string tag;
            if (!(iss >> tag)) {
                continue;
            }

            if (tag == "v") {
                Vec3 position;
                if (!(iss >> position.x >> position.y >> position.z)) {
                    std::cout << "GraphObject::readFromObj: Invalid vertex line: " << line << std::endl;
                    continue;
                }

                float r, g, b;
                Color color = m_defaultVertexColor;
                if (iss >> r >> g >> b) {
                    color = Color(r, g, b);
                }

                m_graphData->vertices.emplace_back(position, color);
            }
            else if (tag == "l") {
                const int vertexCount = static_cast<int>(m_graphData->vertices.size());
                std::vector<int> indices;
                std::string token;
                while (iss >> token) {
                    if (token.empty()) {
                        continue;
                    }

                    size_t slashPos = token.find('/');
                    if (slashPos != std::string::npos) {
                        token = token.substr(0, slashPos);
                    }

                    std::istringstream tokenStream(token);
                    int idx = 0;
                    if (!(tokenStream >> idx)) {
                        std::cout << "GraphObject::readFromObj: Failed to parse index token: " << token << std::endl;
                        continue;
                    }

                    if (idx < 0) {
                        idx = vertexCount + idx + 1;
                    }

                    idx -= 1;
                    if (idx < 0 || idx >= vertexCount) {
                        std::cout << "GraphObject::readFromObj: Invalid vertex index: " << token << std::endl;
                        continue;
                    }

                    indices.push_back(idx);
                }

                for (size_t i = 1; i < indices.size(); ++i) {
                    int a = indices[i - 1];
                    int b = indices[i];

                    if (a != b) {
                        m_graphData->edges.emplace_back(a, b);
                    }
                }
            }
        }

        calculateBounds();
        updateTopologyFlags();

        std::cout << "Loaded graph from OBJ: " << filename
                  << " (" << m_graphData->vertices.size() << " vertices, "
                  << m_graphData->edges.size() << " edges)" << std::endl;
    }

    void GraphObject::writeToObj(const std::string& filename) const {
        if (!m_graphData) {
            std::cout << "No graph data to write" << std::endl;
            return;
        }

        std::filesystem::path outPath(filename);
        if (outPath.has_parent_path()) {
            std::error_code ec;
            std::filesystem::create_directories(outPath.parent_path(), ec);
            if (ec) {
                std::cerr << "Failed to create directory '"
                          << outPath.parent_path().string()
                          << "': " << ec.message() << "\n";
                return;
            }
        }

        std::ofstream output(filename, std::ios::out | std::ios::trunc);
        if (!output) {
            std::cout << "Failed to open OBJ file: " << filename << std::endl;
            return;
        }

        for (const auto& vertex : m_graphData->vertices) {
            output << "v "
                   << vertex.position.x << ' '
                   << vertex.position.y << ' '
                   << vertex.position.z << ' '
                   << vertex.color.r << ' '
                   << vertex.color.g << ' '
                   << vertex.color.b << '\n';
        }

        auto makeEdgeKey = [](int a, int b) {
            if (a > b) {
                std::swap(a, b);
            }
            return (static_cast<std::uint64_t>(a) << 32) | static_cast<std::uint32_t>(b);
        };

        std::unordered_set<std::uint64_t> uniqueEdges;
        uniqueEdges.reserve(m_graphData->edges.size());

        const int vertexCount = static_cast<int>(m_graphData->vertices.size());
        for (const auto& edge : m_graphData->edges) {
            int a = edge.vertexA;
            int b = edge.vertexB;
            if (a < 0 || b < 0 || a >= vertexCount || b >= vertexCount || a == b) {
                continue;
            }

            if (!uniqueEdges.insert(makeEdgeKey(a, b)).second) {
                continue;
            }

            output << "l " << (a + 1) << ' ' << (b + 1) << '\n';
        }

        std::cout << "Successfully exported graph to: " << filename << std::endl;
    }
    void GraphObject::weld(float epsilon) {
        if (!m_graphData || m_graphData->vertices.size() < 2) {
            return;
        }

        if (epsilon <= 0.0f) {
            epsilon = 1e-6f;
        }

        struct Key {
            int x;
            int y;
            int z;

            bool operator==(const Key& other) const {
                return x == other.x && y == other.y && z == other.z;
            }
        };

        struct KeyHash {
            std::size_t operator()(const Key& key) const {
                std::size_t hx = static_cast<std::size_t>(key.x) * 73856093u;
                std::size_t hy = static_cast<std::size_t>(key.y) * 19349663u;
                std::size_t hz = static_cast<std::size_t>(key.z) * 83492791u;
                return hx ^ hy ^ hz;
            }
        };

        const float invEpsilon = 1.0f / epsilon;
        std::unordered_map<Key, int, KeyHash> keyToIndex;
        keyToIndex.reserve(m_graphData->vertices.size());

        std::vector<Vec3> positionSum;
        std::vector<Color> colorSum;
        std::vector<int> counts;
        std::vector<int> remap(m_graphData->vertices.size(), -1);

        positionSum.reserve(m_graphData->vertices.size());
        colorSum.reserve(m_graphData->vertices.size());
        counts.reserve(m_graphData->vertices.size());

        auto quantize = [invEpsilon](const Vec3& position) {
            return Key{
                static_cast<int>(std::floor(position.x * invEpsilon + 0.5f)),
                static_cast<int>(std::floor(position.y * invEpsilon + 0.5f)),
                static_cast<int>(std::floor(position.z * invEpsilon + 0.5f))};
        };

        for (size_t i = 0; i < m_graphData->vertices.size(); ++i) {
            const auto& vertex = m_graphData->vertices[i];
            Key key = quantize(vertex.position);
            auto it = keyToIndex.find(key);
            if (it == keyToIndex.end()) {
                int newIndex = static_cast<int>(positionSum.size());
                keyToIndex.emplace(key, newIndex);
                positionSum.push_back(vertex.position);
                colorSum.push_back(vertex.color);
                counts.push_back(1);
                remap[i] = newIndex;
            } else {
                int index = it->second;
                positionSum[index] += vertex.position;
                colorSum[index] += vertex.color;
                counts[index] += 1;
                remap[i] = index;
            }
        }

        std::vector<GraphVertex> newVertices;
        newVertices.reserve(positionSum.size());
        for (size_t i = 0; i < positionSum.size(); ++i) {
            const float invCount = 1.0f / static_cast<float>(counts[i]);
            Vec3 averagedPosition = positionSum[i] * invCount;
            Color averagedColor(colorSum[i].r * invCount,
                                colorSum[i].g * invCount,
                                colorSum[i].b * invCount,
                                colorSum[i].a * invCount);
            newVertices.emplace_back(averagedPosition, averagedColor);
        }

        auto makeEdgeKey = [](int a, int b) {
            if (a > b) {
                std::swap(a, b);
            }
            return (static_cast<std::uint64_t>(a) << 32) | static_cast<std::uint32_t>(b);
        };

        std::unordered_set<std::uint64_t> uniqueEdges;
        uniqueEdges.reserve(m_graphData->edges.size());

        std::vector<GraphEdge> newEdges;
        newEdges.reserve(m_graphData->edges.size());

        for (const auto& edge : m_graphData->edges) {
            if (edge.vertexA < 0 || edge.vertexB < 0 ||
                edge.vertexA >= static_cast<int>(remap.size()) ||
                edge.vertexB >= static_cast<int>(remap.size())) {
                continue;
            }

            int a = remap[edge.vertexA];
            int b = remap[edge.vertexB];
            if (a == b || a < 0 || b < 0) {
                continue;
            }

            std::uint64_t edgeKey = makeEdgeKey(a, b);
            if (uniqueEdges.insert(edgeKey).second) {
                newEdges.emplace_back(a, b);
            }
        }

        m_graphData->vertices = std::move(newVertices);
        m_graphData->edges = std::move(newEdges);

        calculateBounds();
        updateTopologyFlags();
    }

    void GraphObject::combineWith(const GraphObject& other) {
        if (!other.m_graphData || other.m_graphData->vertices.empty()) {
            return;
        }

        if (!m_graphData) {
            m_graphData = std::make_shared<GraphData>();
        }

        const size_t vertexOffset = m_graphData->vertices.size();
        m_graphData->vertices.reserve(vertexOffset + other.m_graphData->vertices.size());
        m_graphData->edges.reserve(m_graphData->edges.size() + other.m_graphData->edges.size());

        m_graphData->vertices.insert(m_graphData->vertices.end(),
                                     other.m_graphData->vertices.begin(),
                                     other.m_graphData->vertices.end());

        for (const auto& edge : other.m_graphData->edges) {
            if (edge.vertexA < 0 || edge.vertexB < 0 ||
                edge.vertexA >= static_cast<int>(other.m_graphData->vertices.size()) ||
                edge.vertexB >= static_cast<int>(other.m_graphData->vertices.size())) {
                continue;
            }
            int offsetA = static_cast<int>(vertexOffset + static_cast<size_t>(edge.vertexA));
            int offsetB = static_cast<int>(vertexOffset + static_cast<size_t>(edge.vertexB));
            m_graphData->edges.emplace_back(offsetA, offsetB);
        }

        calculateBounds();
        updateTopologyFlags();
    }

    void GraphObject::resample(float sampleDistance) {
        if (!m_graphData || sampleDistance <= 0.0f) {
            return;
        }

        updateTopologyFlags();
        if (!m_isPolyline) {
            return;
        }

        std::vector<std::vector<int>> adjacency;
        std::vector<int> degree;
        size_t validEdgeCount = 0;
        if (!buildAdjacency(adjacency, degree, validEdgeCount) || validEdgeCount == 0) {
            return;
        }

        auto order = buildPolylineOrder(adjacency, degree, validEdgeCount);
        if (order.size() < 2) {
            return;
        }

        const bool closedLoop = m_isClosed;
        if (closedLoop && order.front() != order.back()) {
            order.push_back(order.front());
        }
        if (!closedLoop && order.front() == order.back() && order.size() > 1) {
            order.pop_back();
        }

        std::vector<Vec3> positions;
        std::vector<Color> colors;
        positions.reserve(order.size());
        colors.reserve(order.size());

        for (int index : order) {
            if (index < 0 || index >= static_cast<int>(m_graphData->vertices.size())) {
                return;
            }
            const auto& vertex = m_graphData->vertices[static_cast<size_t>(index)];
            positions.push_back(vertex.position);
            colors.push_back(vertex.color);
        }

        if (closedLoop && (positions.size() < 3 || !(positions.front() == positions.back()))) {
            positions.push_back(positions.front());
            colors.push_back(colors.front());
        }

        if (!closedLoop && positions.size() < 2) {
            return;
        }

        std::vector<float> cumulative;
        cumulative.reserve(positions.size());
        cumulative.push_back(0.0f);
        float totalLength = 0.0f;
        for (size_t i = 1; i < positions.size(); ++i) {
            float segmentLength = (positions[i] - positions[i - 1]).length();
            totalLength += segmentLength;
            cumulative.push_back(totalLength);
        }

        const float lengthEps = 1e-6f;
        if (totalLength <= lengthEps) {
            return;
        }

        std::vector<float> targets;
        targets.push_back(0.0f);
        for (float distance = sampleDistance; distance < totalLength - lengthEps; distance += sampleDistance) {
            targets.push_back(distance);
        }

        if (!closedLoop) {
            if (totalLength - targets.back() > lengthEps) {
                targets.push_back(totalLength);
            }
        } else {
            if (targets.size() < 3) {
                return;
            }
        }

        auto sampleAt = [&](float distance) {
            auto it = std::lower_bound(cumulative.begin(), cumulative.end(), distance);
            size_t segmentIndex = 0;
            if (it == cumulative.end()) {
                segmentIndex = cumulative.size() - 2;
            } else if (*it == distance) {
                segmentIndex = std::distance(cumulative.begin(), it);
                if (segmentIndex == cumulative.size() - 1) {
                    segmentIndex -= 1;
                }
            } else {
                segmentIndex = static_cast<size_t>(std::distance(cumulative.begin(), it) - 1);
            }

            const float segmentStart = cumulative[segmentIndex];
            const float segmentEnd = cumulative[segmentIndex + 1];
            const float denominator = std::max(segmentEnd - segmentStart, lengthEps);
            const float t = (distance - segmentStart) / denominator;

            Vec3 position = Vec3::lerp(positions[segmentIndex], positions[segmentIndex + 1], t);
            Color color = Color::lerp(colors[segmentIndex], colors[segmentIndex + 1], t);
            return GraphVertex(position, color);
        };

        std::vector<GraphVertex> newVertices;
        newVertices.reserve(targets.size() + (closedLoop ? 1 : 0));
        for (float distance : targets) {
            newVertices.push_back(sampleAt(distance));
        }

        std::vector<GraphEdge> newEdges;
        newEdges.reserve(newVertices.size() + (closedLoop ? 1 : 0));
        for (size_t i = 1; i < newVertices.size(); ++i) {
            newEdges.emplace_back(static_cast<int>(i - 1), static_cast<int>(i));
        }
        if (closedLoop && newVertices.size() > 2) {
            newEdges.emplace_back(static_cast<int>(newVertices.size() - 1), 0);
        }

        m_graphData->vertices = std::move(newVertices);
        m_graphData->edges = std::move(newEdges);

        calculateBounds();
        updateTopologyFlags();
    }

    void GraphObject::resampleByCount(int sampleCount) {
        if (!m_graphData || sampleCount <= 2) {
            return;
        }

        float sampleDistance = getLength() / static_cast<float>(sampleCount);
        resample(sampleDistance);
    }


    std::vector<GraphObject> GraphObject::separate() const {
        std::vector<GraphObject> components;
        if (!m_graphData) {
            return components;
        }

        const size_t vertexCount = m_graphData->vertices.size();
        if (vertexCount == 0) {
            return components;
        }

        std::vector<std::vector<int>> adjacency;
        std::vector<int> degree;
        size_t validEdgeCount = 0;
        buildAdjacency(adjacency, degree, validEdgeCount);

        std::vector<bool> visited(vertexCount, false);

        auto makeEdgeKey = [](int a, int b) {
            if (a > b) {
                std::swap(a, b);
            }
            return (static_cast<std::uint64_t>(a) << 32) | static_cast<std::uint32_t>(b);
        };

        for (size_t start = 0; start < vertexCount; ++start) {
            if (visited[start]) {
                continue;
            }

            std::queue<int> bfs;
            std::vector<int> componentVertices;
            bfs.push(static_cast<int>(start));
            visited[start] = true;

            while (!bfs.empty()) {
                int current = bfs.front();
                bfs.pop();
                componentVertices.push_back(current);

                if (current >= static_cast<int>(adjacency.size())) {
                    continue;
                }

                for (int neighbor : adjacency[static_cast<size_t>(current)]) {
                    if (!visited[static_cast<size_t>(neighbor)]) {
                        visited[static_cast<size_t>(neighbor)] = true;
                        bfs.push(neighbor);
                    }
                }
            }

            std::unordered_map<int, int> remap;
            remap.reserve(componentVertices.size());

            auto componentData = std::make_shared<GraphData>();
            componentData->vertices.reserve(componentVertices.size());

            for (int originalIndex : componentVertices) {
                remap[originalIndex] = static_cast<int>(componentData->vertices.size());
                componentData->vertices.push_back(m_graphData->vertices[static_cast<size_t>(originalIndex)]);
            }

            std::unordered_set<std::uint64_t> edgeSet;
            edgeSet.reserve(m_graphData->edges.size());

            for (const auto& edge : m_graphData->edges) {
                auto itA = remap.find(edge.vertexA);
                auto itB = remap.find(edge.vertexB);
                if (itA == remap.end() || itB == remap.end()) {
                    continue;
                }
                int a = itA->second;
                int b = itB->second;
                if (a == b) {
                    continue;
                }
                std::uint64_t key = makeEdgeKey(a, b);
                if (edgeSet.insert(key).second) {
                    componentData->edges.emplace_back(a, b);
                }
            }

            GraphObject component;
            component.setGraphData(componentData);
            component.m_showVertices = m_showVertices;
            component.m_showEdges = m_showEdges;
            component.m_vertexSize = m_vertexSize;
            component.m_edgeWidth = m_edgeWidth;
            component.m_defaultVertexColor = m_defaultVertexColor;
            component.m_edgeColor = m_edgeColor;

            components.push_back(std::move(component));
        }

        return components;
    }

    void GraphObject::createFromPositionsAndEdges(const std::vector<Vec3>& positions,
                                                  const std::vector<std::pair<int, int>>& edges,
                                                  const std::vector<Color>& colors) {
        if (!m_graphData) {
            m_graphData = std::make_shared<GraphData>();
        }

        m_graphData->clear();
        m_graphData->vertices.reserve(positions.size());

        for (size_t i = 0; i < positions.size(); ++i) {
            Color vertexColor = (i < colors.size()) ? colors[i] : m_defaultVertexColor;
            m_graphData->vertices.emplace_back(positions[i], vertexColor);
        }

        m_graphData->edges.reserve(edges.size());
        for (const auto& edge : edges) {
            m_graphData->addEdge(edge.first, edge.second);
        }

        calculateBounds();
        updateTopologyFlags();
    }

    int GraphObject::addVertex(const Vec3& position, const Color& color) {
        if (!m_graphData) {
            m_graphData = std::make_shared<GraphData>();
        }

        int index = m_graphData->addVertex(position, color);
        calculateBounds();
        updateTopologyFlags();
        return index;
    }

    int GraphObject::addEdge(int vertexA, int vertexB) {
        if (!m_graphData) {
            return -1;
        }

        int index = m_graphData->addEdge(vertexA, vertexB);
        if (index >= 0) {
            updateTopologyFlags();
        }
        return index;
    }

    void GraphObject::renderImpl(Renderer& renderer, Camera& camera) {
        (void)camera;

        if (!m_graphData || m_graphData->vertices.empty()) {
            std::cout << "GraphObject::renderImpl: No graph data" << std::endl;
            return;
        }

        if (m_showEdges) {
            renderEdges(renderer);
        }

        if (m_showVertices) {
            renderVertices(renderer);
        }
    }

    void GraphObject::calculateBounds() {
        if (!m_graphData) {
            setBounds(Vec3(-0.5f, -0.5f, -0.5f), Vec3(0.5f, 0.5f, 0.5f));
            return;
        }

        Vec3 minBounds;
        Vec3 maxBounds;
        m_graphData->updateBounds(minBounds, maxBounds);
        setBounds(minBounds, maxBounds);
    }

    void GraphObject::renderVertices(Renderer& renderer) {
        renderer.setPointSize(m_vertexSize);

        for (const auto& vertex : m_graphData->vertices) {
            Color drawColor = vertex.color;
            if (drawColor.a <= 0.0f) {
                drawColor.a = 1.0f;
            }
            renderer.drawPoint(vertex.position, drawColor, m_vertexSize);
        }
    }

    void GraphObject::renderEdges(Renderer& renderer) {
        renderer.setLineWidth(m_edgeWidth);

        for (const auto& edge : m_graphData->edges) {
            if (edge.vertexA < 0 || edge.vertexB < 0 ||
                edge.vertexA >= static_cast<int>(m_graphData->vertices.size()) ||
                edge.vertexB >= static_cast<int>(m_graphData->vertices.size())) {
                continue;
            }

            const Vec3& start = m_graphData->vertices[edge.vertexA].position;
            const Vec3& end = m_graphData->vertices[edge.vertexB].position;
            renderer.drawLine(start, end, m_edgeColor, m_edgeWidth);
        }
    }

    void GraphObject::updateTopologyFlags() {
        m_isClosed = false;
        m_isPolyline = false;

        if (!m_graphData || m_graphData->vertices.empty()) {
            return;
        }

        std::vector<std::vector<int>> adjacency;
        std::vector<int> degree;
        size_t validEdgeCount = 0;
        if (!buildAdjacency(adjacency, degree, validEdgeCount) || validEdgeCount == 0) {
            return;
        }

        int degreeOneCount = 0;
        int activeVertexCount = 0;
        for (int valency : degree) {
            if (valency < 0) {
                continue;
            }
            if (valency > 0) {
                ++activeVertexCount;
                if (valency == 1) {
                    ++degreeOneCount;
                } else if (valency != 2) {
                    return;
                }
            }
        }

        if (activeVertexCount == 0) {
            return;
        }

        std::vector<char> visited(m_graphData->vertices.size(), 0);
        std::queue<int> queue;
        int start = -1;
        for (size_t i = 0; i < degree.size(); ++i) {
            if (degree[i] > 0) {
                start = static_cast<int>(i);
                break;
            }
        }
        if (start == -1) {
            return;
        }

        queue.push(start);
        visited[start] = 1;
        int visitedCount = 1;
        while (!queue.empty()) {
            int current = queue.front();
            queue.pop();
            for (int neighbor : adjacency[current]) {
                if (!visited[neighbor]) {
                    visited[neighbor] = 1;
                    queue.push(neighbor);
                    ++visitedCount;
                }
            }
        }

        if (visitedCount != activeVertexCount) {
            return;
        }

        if (degreeOneCount == 2 && validEdgeCount == static_cast<size_t>(activeVertexCount - 1)) {
            m_isPolyline = true;
            m_isClosed = false;
        } else if (degreeOneCount == 0 && validEdgeCount == static_cast<size_t>(activeVertexCount)) {
            m_isPolyline = true;
            m_isClosed = true;
        }
    }

    bool GraphObject::buildAdjacency(std::vector<std::vector<int>>& adjacency,
                                     std::vector<int>& degree,
                                     size_t& validEdgeCount) const {
        adjacency.clear();
        degree.clear();
        validEdgeCount = 0;

        if (!m_graphData) {
            return false;
        }

        const size_t vertexCount = m_graphData->vertices.size();
        adjacency.assign(vertexCount, {});
        degree.assign(vertexCount, 0);

        auto makeEdgeKey = [](int a, int b) {
            if (a > b) {
                std::swap(a, b);
            }
            return (static_cast<std::uint64_t>(a) << 32) | static_cast<std::uint32_t>(b);
        };

        std::unordered_set<std::uint64_t> uniqueEdges;
        uniqueEdges.reserve(m_graphData->edges.size());

        bool valid = true;
        for (const auto& edge : m_graphData->edges) {
            int a = edge.vertexA;
            int b = edge.vertexB;
            if (a < 0 || b < 0 ||
                a >= static_cast<int>(vertexCount) ||
                b >= static_cast<int>(vertexCount) ||
                a == b) {
                valid = false;
                continue;
            }

            std::uint64_t key = makeEdgeKey(a, b);
            if (!uniqueEdges.insert(key).second) {
                continue;
            }

            adjacency[static_cast<size_t>(a)].push_back(b);
            adjacency[static_cast<size_t>(b)].push_back(a);
            degree[static_cast<size_t>(a)] += 1;
            degree[static_cast<size_t>(b)] += 1;
            ++validEdgeCount;
        }

        return valid;
    }

    std::vector<int> GraphObject::buildPolylineOrder(const std::vector<std::vector<int>>& adjacency,
                                                     const std::vector<int>& degree,
                                                     size_t validEdgeCount) const {
        std::vector<int> order;
        if (!m_graphData || adjacency.empty()) {
            return order;
        }

        int start = -1;
        bool closedLoop = true;
        for (size_t i = 0; i < degree.size(); ++i) {
            if (degree[i] == 1) {
                start = static_cast<int>(i);
                closedLoop = false;
                break;
            }
        }
        if (start == -1) {
            for (size_t i = 0; i < degree.size(); ++i) {
                if (degree[i] > 0) {
                    start = static_cast<int>(i);
                    closedLoop = true;
                    break;
                }
            }
        }
        if (start == -1) {
            return order;
        }

        order.reserve(validEdgeCount + 1);
        order.push_back(start);

        auto makeEdgeKey = [](int a, int b) {
            if (a > b) {
                std::swap(a, b);
            }
            return (static_cast<std::uint64_t>(a) << 32) | static_cast<std::uint32_t>(b);
        };

        std::unordered_set<std::uint64_t> visitedEdges;
        visitedEdges.reserve(validEdgeCount);

        int previous = -1;
        int current = start;
        size_t traversed = 0;
        while (traversed < validEdgeCount) {
            int next = -1;
            for (int neighbor : adjacency[static_cast<size_t>(current)]) {
                if (neighbor == previous) {
                    continue;
                }
                std::uint64_t edgeKey = makeEdgeKey(current, neighbor);
                if (visitedEdges.insert(edgeKey).second) {
                    next = neighbor;
                    break;
                }
            }

            if (next == -1) {
                break;
            }

            order.push_back(next);
            previous = current;
            current = next;
            ++traversed;

            if (closedLoop && current == start) {
                break;
            }
        }

        if (closedLoop && (order.front() != order.back())) {
            order.push_back(order.front());
        }

        return order;
    }

} // namespace alice2

