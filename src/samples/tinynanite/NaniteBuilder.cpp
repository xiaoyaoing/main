#include "NaniteBuilder.h"

#include "meshoptimizer.h"
#include "Core/RenderContext.h"
#include "Scene/SceneLoader/ObjLoader.hpp"
#include "Scene/SceneLoader/SceneLoaderInterface.h"

#include <unordered_map>
#include <unordered_set>
#include <metis.h>
#include <numeric>

static constexpr uint32_t ClusterSize = 128;

struct BuildCluster {
};

struct AdjVertex {
    std::unordered_set<uint32_t> adjVertices;
};

static __forceinline uint32_t MurmurFinalize32(uint32_t Hash) {
    Hash ^= Hash >> 16;
    Hash *= 0x85ebca6b;
    Hash ^= Hash >> 13;
    Hash *= 0xc2b2ae35;
    Hash ^= Hash >> 16;
    return Hash;
}

static __forceinline uint32_t Murmur32(std::initializer_list<uint32_t> InitList) {
    uint32_t Hash = 0;
    for (auto Element : InitList) {
        Element *= 0xcc9e2d51;
        Element = (Element << 15) | (Element >> (32 - 15));
        Element *= 0x1b873593;

        Hash ^= Element;
        Hash = (Hash << 13) | (Hash >> (32 - 13));
        Hash = Hash * 5 + 0xe6546b64;
    }

    return MurmurFinalize32(Hash);
}

size_t HashCombine(uint32_t hash0, uint32_t hash1) {
    return size_t(hash0) | (size_t(hash1) << 32);
}

void SaveMeshInputDataToObj(const MeshInputData& meshData, const std::string& filePath) {
    std::ofstream file(filePath);
    if (!file.is_open()) {
        LOGE("Failed to open file for writing: {}", filePath.c_str());
        return;
    }

    // Write header comment
    file << "# Exported from MeshInputData\n";
    file << "# Vertices: " << meshData.Vertices.Positions.size() << "\n";
    file << "# Triangles: " << meshData.TriangleIndices.size() / 3 << "\n\n";

    // Write vertices
    for (const auto& pos : meshData.Vertices.Positions) {
        file << "v " << pos.x << " " << pos.y << " " << pos.z << "\n";
    }

    // Write texture coordinates
    for (const auto& uv : meshData.Vertices.UVs) {
        file << "vt " << uv.x << " " << uv.y << "\n";
    }

    // Write normals
    for (const auto& normal : meshData.Vertices.Normals) {
        file << "vn " << normal.x << " " << normal.y << " " << normal.z << "\n";
    }

    // Write faces
    // OBJ format is 1-based indexing, so we need to add 1 to all indices
    uint32_t baseTriangle = 0;
    for (uint32_t meshIndex = 0; meshIndex < meshData.TriangleCounts.size(); meshIndex++) {
        file << "\n# Mesh " << meshIndex << " (Material Index: "
             << (meshIndex < meshData.MaterialIndices.size() ? std::to_string(meshData.MaterialIndices[meshIndex]) : "N/A")
             << ")\n";

        for (uint32_t i = 0; i < meshData.TriangleCounts[meshIndex]; i++) {
            uint32_t idx0 = meshData.TriangleIndices[baseTriangle + i * 3 + 0] + 1;
            uint32_t idx1 = meshData.TriangleIndices[baseTriangle + i * 3 + 1] + 1;
            uint32_t idx2 = meshData.TriangleIndices[baseTriangle + i * 3 + 2] + 1;

            // Format: f v1/vt1/vn1 v2/vt2/vn2 v3/vt3/vn3
            file << "f "
                 << idx0 << "/" << idx0 << "/" << idx0 << " "
                 << idx1 << "/" << idx1 << "/" << idx1 << " "
                 << idx2 << "/" << idx2 << "/" << idx2 << "\n";
        }
        baseTriangle += meshData.TriangleCounts[meshIndex] * 3;
    }

    file.close();
    LOGI("Successfully saved mesh to: {}", filePath.c_str());
}

void saveSignleClusterToObj(Cluster& cluster, const std::string& filename) {
    MeshInputData inputData;
    inputData.Vertices.Positions = cluster.m_positions;
    inputData.Vertices.Normals   = cluster.m_normals;
    inputData.Vertices.UVs       = cluster.m_uvs;
    inputData.TriangleIndices    = cluster.m_indexes;
    inputData.TriangleCounts.push_back(cluster.m_indexes.size() / 3);
    SaveMeshInputDataToObj(inputData, filename);
}

uint32_t HashPosition(const glm::vec3& Position) {
    union {
        float    f;
        uint32_t i;
    } x;
    union {
        float    f;
        uint32_t i;
    } y;
    union {
        float    f;
        uint32_t i;
    } z;

    x.f = Position.x;
    y.f = Position.y;
    z.f = Position.z;

    return Murmur32({Position.x == 0.0f ? 0u : x.i,
                     Position.y == 0.0f ? 0u : y.i,
                     Position.z == 0.0f ? 0u : z.i});
}

inline uint32_t cycle3(uint32_t Value) {
    uint32_t ValueMod3  = Value % 3;
    uint32_t Value1Mod3 = (1 << ValueMod3) & 3;
    return Value - ValueMod3 + Value1Mod3;
}

struct PointHash {
    std::unordered_map<size_t, std::vector<uint32_t>>                                    hashTable1;
    std::unordered_map<size_t, std::vector<std::tuple<glm::vec3, glm::vec3, glm::vec3>>> debugPositionTable;
    template<typename GerPosition, typename Function>
    void ForAllMatching(uint32_t index, bool bAdd, GerPosition&& getPosition, Function&& callback) {

        vec3 position0 = getPosition(index);
        vec3 position1 = getPosition(cycle3(index));

        uint32 hash0 = HashPosition(position0);
        uint32 hash1 = HashPosition(position1);
        auto   hash  = HashCombine(hash0, hash1);

        if (hashTable1.contains(hash)) {
            for (auto& anotherEdge : hashTable1[hash]) {
                callback(index, anotherEdge);
            }
        } else {
        }

        hash = HashCombine(hash1, hash0);

        if (bAdd) {
            if (!hashTable1.contains(hash)) {
                hashTable1[hash] = std::vector<uint32_t>();
            }
            hashTable1[hash].push_back(index);

            // if(debugPositionTable.contains(hash)) {
            //     debugPositionTable[hash].push_back({position0, position1, position2});
            // } else {
            //     debugPositionTable[hash] = std::vector<std::tuple<glm::vec3,glm::vec3,glm::vec3>>();
            //     debugPositionTable[hash].push_back({position0, position1, position2});
            // }
        }
    }

    template<typename GerPosition, typename Function>
    void ForAllMatchingPoint(uint32_t index, bool bAdd, GerPosition&& getPosition, Function&& callback) {
        vec3 position0 = getPosition(index);
        auto hash0     = HashPosition(position0);
        if (hashTable1.contains(hash0)) {
            for (auto& anotherEdge : hashTable1[hash0]) {
                callback(index, anotherEdge);
            }
        }
        if (bAdd) {
            if (!hashTable1.contains(hash0)) {
                hashTable1[hash0] = std::vector<uint32_t>();
            }
            hashTable1[hash0].push_back(index);

            auto position1 = getPosition(cycle3(index));
            auto position2 = getPosition(cycle3(cycle3(index)));
            if (debugPositionTable.contains(hash0)) {
                debugPositionTable[hash0].push_back({position0, position1, position2});
            } else {
               
                if(position0 == position1 && position1 == position2) {
                    int k = 1;
                }
                debugPositionTable[hash0] = std::vector<std::tuple<glm::vec3, glm::vec3, glm::vec3>>();
                debugPositionTable[hash0].push_back({position0, position1, position2});
            }
        }
    }
};

uint32_t getTriangleIndexByVertexIndex(uint32_t vertexIndex) {
    return vertexIndex / 3;
}

template<typename GerPosition>
GraphAdjancy buildAdjancy(std::span<uint32_t> indexes, GerPosition&& getPosition) {
    GraphAdjancy graphData(indexes.size());
    PointHash    hash;
    for (uint32_t i = 0; i < indexes.size(); i += 3) {
        for (int k = 0; k < 3; k++) {
            hash.ForAllMatching(i + k, true, [&](uint32_t index) { return getPosition(index); }, [&](uint32_t edgeIndex, uint32_t otherEdgeIndex) { graphData.addEdge(edgeIndex, otherEdgeIndex); });
        }
    }
    std::vector<uint32_t> adjOffsets;
    uint32_t              offset = 0;
    for (uint32_t i = 0; i < graphData.adjVertices.size(); i++) {
        adjOffsets.push_back(offset);
        offset += graphData.adjVertices[i].adjVertices.size();
    }
    return graphData;
}

// GraphAdjancy buildClusterGroupAdjancy(std::span<Cluster> clusters) {
//     std::vector<ClusterExternEdge> externEdges;
//     uint32_t                       externalEdgeCount = 0;
//     PointHash                      edgehash;
//     GraphAdjancy                   graphData(clusters.size());
//     for (uint32_t i = 0; i < clusters.size(); i++) {
//         externalEdgeCount += clusters[i].m_external_edges.size();
//     }
//     externEdges.resize(externalEdgeCount);
//     uint32_t externalEdgeOffset = 0;
//     for (uint32_t i = 0; i < clusters.size(); i++) {
//         for (auto edge : clusters[i].m_external_edges) {
//             uint32_t index0                   = clusters[i].getIndexes(edge.v0);
//             uint32_t index1                   = clusters[i].getIndexes(edge.v1);
//             auto     position0                = clusters[i].getPosition(index0);
//             auto     position1                = clusters[i].getPosition(index1);
//             auto     hash_0                   = HashPosition(position0);
//             auto     hash_1                   = HashPosition(position1);
//             auto     hash_value               = HashCombine(hash_0, hash_1);
//             externEdges[externalEdgeOffset++] = edge;
//             edgehash.hashTable1[hash_value].push_back(externalEdgeOffset);
//         }
//     }
//
//     for (uint32_t i = 0; i < clusters.size(); i++) {
//         for (auto edge : clusters[i].m_external_edges) {
//             auto     hash_0     = HashPosition(clusters[i].m_positions[edge.v0]);
//             auto     hash_1     = HashPosition(clusters[i].m_positions[edge.v1]);
//             auto     hash_value = Murmur32({hash_1, hash_0});
//             uint32_t index0     = clusters[i].getIndexes(edge.v0);
//             uint32_t index1     = clusters[i].getIndexes(edge.v1);
//             auto     position0  = clusters[i].getPosition(index0);
//             auto     position1  = clusters[i].getPosition(index1);
//             for (uint32_t group : edgehash.hashTable1[hash_value]) {
//                 auto externalEdge   = externEdges[group];
//                 auto otherCluster   = clusters[externalEdge.clusterIndex];
//                 auto otherIndex0    = otherCluster.getIndexes(externalEdge.v0);
//                 auto otherIndex1    = otherCluster.getIndexes(externalEdge.v1);
//                 auto otherPosition0 = otherCluster.getPosition(otherIndex0);
//                 auto otherPosition1 = otherCluster.getPosition(otherIndex1);
//                 if (externalEdge.clusterIndex != i && position0 == otherPosition0 && position1 == otherPosition1) {
//                     graphData.addEdge(i, externalEdge.clusterIndex);
//                     clusters[i].m_linked_cluster.insert(externalEdge.clusterIndex);
//                 }
//             }
//         }
//     }
//
//     return graphData;
// }
// GraphAdjancy buildClusterGroupAdjancy1(std::span<Cluster> clusters, uint32_t levelOffset) {
//     GraphAdjancy graphData(clusters.size());
//
//     // 遍历所有cluster
//     for (uint32_t localId = 0; localId < clusters.size(); localId++) {
//         const auto& cluster = clusters[localId];
//
//         // 对于每个相邻的cluster
//         for (const auto& globalAdjId : cluster.m_linked_cluster) {
//             // 检查相邻cluster是否在当前span范围内
//             if (globalAdjId >= levelOffset && globalAdjId < levelOffset + clusters.size()) {
//                 uint32_t localAdjId = globalAdjId - levelOffset;
//                 graphData.addEdge(localId, localAdjId);
//             }
//         }
//     }
//
//     return graphData;
// }

struct Triangle {
    vec3 position[3];
};

void PrintTriangle(const Triangle& tri) {
}

bool isTriangleAdjancy(const Triangle& tri0, const Triangle& tri1) {
    uint32_t count = 0;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            if (tri0.position[i] == tri1.position[j]) {
                count++;
            }
        }
    }
    return count >= 1;
}

template<typename GerPosition>
void checkTriangleAdjancy(const GraphPartitioner::FGraphData& graph, const std::vector<uint32_t>& indexes, GerPosition&& getPosition) {
    for (int i = 0; i < graph.AdjacencyOffset.size() - 1; i++) {
        Triangle tri;
        tri.position[0] = getPosition(i * 3);
        tri.position[1] = getPosition(i * 3 + 1);
        tri.position[2] = getPosition(i * 3 + 2);
        for (int j = graph.AdjacencyOffset[i]; j < graph.AdjacencyOffset[i + 1]; j++) {
            auto     adjIndex = graph.Adjacency[j];
            Triangle adjTri;
            adjTri.position[0] = getPosition(adjIndex * 3);
            adjTri.position[1] = getPosition(adjIndex * 3 + 1);
            adjTri.position[2] = getPosition(adjIndex * 3 + 2);
            bool isAdj         = isTriangleAdjancy(tri, adjTri);
            if (!isAdj) {
                LOGE("Triangle {} and {} is not adjancy", i, adjIndex);
            }
        }
    }
}

void clusterTriangles2(std::vector<Cluster>& clusters, MeshInputData& InputMeshData, uint32_t baseTriangle, uint32_t numTriangles) {
    if (numTriangles == 0) {
        LOGE("No triangles to cluster");
        return;
    }

    uint32_t targetClusterCount = std::max(1u, (numTriangles + ClusterSize - 1) / ClusterSize);

    GraphPartitioner partitioner(numTriangles, targetClusterCount);
    auto             graph = partitioner.NewGraph(numTriangles * 6);

    if (!graph) {
        LOGE("Failed to create graph");
        return;
    }

    graph->AdjacencyOffset[0] = 0;

    std::vector<std::vector<uint32_t>> triangleAdjacency(numTriangles);

    for (uint32_t i = 0; i < numTriangles; i++) {
        uint32_t i0 = InputMeshData.TriangleIndices[baseTriangle + i * 3 + 0];
        uint32_t i1 = InputMeshData.TriangleIndices[baseTriangle + i * 3 + 1];
        uint32_t i2 = InputMeshData.TriangleIndices[baseTriangle + i * 3 + 2];

        for (uint32_t j = 0; j < numTriangles; j++) {
            if (i == j) continue;

            uint32_t j0 = InputMeshData.TriangleIndices[baseTriangle + j * 3 + 0];
            uint32_t j1 = InputMeshData.TriangleIndices[baseTriangle + j * 3 + 1];
            uint32_t j2 = InputMeshData.TriangleIndices[baseTriangle + j * 3 + 2];

            bool isAdjacent = false;
            isAdjacent |= (i0 == j0 && i1 == j1) || (i0 == j1 && i1 == j0);
            isAdjacent |= (i1 == j1 && i2 == j2) || (i1 == j2 && i2 == j1);
            isAdjacent |= (i2 == j2 && i0 == j0) || (i2 == j0 && i0 == j2);

            if (isAdjacent) {
                triangleAdjacency[i].push_back(j);
            }
        }
    }

    for (uint32_t i = 0; i < numTriangles; i++) {
        const auto& adjacentTris = triangleAdjacency[i];

        graph->AdjacencyOffset[i + 1] = graph->AdjacencyOffset[i] + adjacentTris.size();

        for (uint32_t adjTri : adjacentTris) {
            float weight = 1.0f;

            graph->Adjacency.push_back(adjTri);
            graph->AdjacencyCost.push_back(weight);
        }
    }

    bool isValid = true;
    for (uint32_t i = 0; i < numTriangles; i++) {
        if (graph->AdjacencyOffset[i] > graph->AdjacencyOffset[i + 1]) {
            LOGE("Invalid adjacency offset at {}", i);
            isValid = false;
            break;
        }
    }

    if (!isValid) {
        delete graph;
        return;
    }

    LOGI("Graph stats:");
    LOGI("  Triangles: {}", numTriangles);
    LOGI("  Target clusters: {}", targetClusterCount);
    LOGI("  Total edges: {}", graph->Adjacency.size());
    LOGI("  Average edges per triangle: %.2f",
         static_cast<float>(graph->Adjacency.size()) / numTriangles);

    partitioner.partition(*graph);

    delete graph;
}

Cluster InitClusterFromMeshInputData(MeshInputData& InputMeshData, uint32_t baseTriangle, uint32_t numTriangles) {
    Cluster cluster;

    for (uint32_t i = 0; i < numTriangles; i++) {
        for (int k = 0; k < 3; k++) {
            uint32_t globalIndex = InputMeshData.TriangleIndices[baseTriangle + i * 3 + k];

            // if (std::find(cluster.m_indexes.begin(), cluster.m_indexes.end(), globalIndex) == cluster.m_indexes.end()) {
            cluster.m_indexes.push_back(globalIndex);
            cluster.m_positions.push_back(InputMeshData.Vertices.Positions[globalIndex]);
            cluster.m_normals.push_back(InputMeshData.Vertices.Normals[globalIndex]);
            cluster.m_uvs.push_back(InputMeshData.Vertices.UVs[globalIndex]);
            // }
        }
    }

    cluster.m_bounding_box = BBox();
    cluster.m_min_pos      = glm::vec3(1e30f);
    cluster.m_max_pos      = glm::vec3(-1e30f);
    cluster.m_mip_level    = InputMeshData.mipLevel;
    for (const auto& pos : cluster.m_positions) {
        cluster.m_bounding_box.unite(pos);
        cluster.m_min_pos = glm::min(cluster.m_min_pos, pos);
        cluster.m_max_pos = glm::max(cluster.m_max_pos, pos);
    }
    return cluster;
}

void clusterTrianglesByMeshOpt(std::vector<Cluster>& clusters, MeshInputData& InputMeshData, uint32_t baseTriangle, uint32_t numTriangles) {
    const size_t                 max_vertices  = 64;
    const size_t                 max_triangles = 124;
    auto                         max_meshlets  = meshopt_buildMeshletsBound(numTriangles * 3, max_vertices, max_triangles);
    std::vector<meshopt_Meshlet> meshlets(max_meshlets);//generated meshlets
    std::vector<uint>            meshlet_vertices;
    // meshlet_vertices.push_back_uninitialized(max_meshlets * max_vertices);
    meshlet_vertices.resize(max_meshlets * max_vertices);
    std::vector<unsigned char> meshlet_triangles;
    meshlet_triangles.resize(max_meshlets * max_triangles);

    float coneWeight = 0.0f;

    size_t meshlet_count = meshopt_buildMeshlets(meshlets.data(),
                                                 meshlet_vertices.data(),
                                                 meshlet_triangles.data(),
                                                 &InputMeshData.TriangleIndices[baseTriangle],
                                                 numTriangles * 3,
                                                 reinterpret_cast<const float*>(InputMeshData.Vertices.Positions.data()),
                                                 InputMeshData.Vertices.Positions.size(),
                                                 sizeof(glm::vec3),
                                                 max_vertices,
                                                 max_triangles,
                                                 coneWeight);

    size_t initialClusterCount = clusters.size();// Store the initial count of clusters

    for (size_t i = 0; i < meshlet_count; ++i) {
        Cluster                cluster;
        const meshopt_Meshlet& meshlet = meshlets[i];

        // Collect vertex positions, normals, and UVs for the current meshlet
        for (size_t j = 0; j < meshlet.vertex_count; ++j) {
            uint32_t vertexIndex = meshlet_vertices[meshlet.vertex_offset + j];
            cluster.m_positions.push_back(InputMeshData.Vertices.Positions[vertexIndex]);
            cluster.m_normals.push_back(InputMeshData.Vertices.Normals[vertexIndex]);
            cluster.m_uvs.push_back(InputMeshData.Vertices.UVs[vertexIndex]);
        }

        // Collect triangle indices for the current meshlet
        for (size_t j = 0; j < meshlet.triangle_count; ++j) {
            for (int k = 0; k < 3; k++) {
                uint32_t triangleIndex = meshlet_triangles[meshlet.triangle_offset + j * 3 + k];
                cluster.m_indexes.push_back(triangleIndex);
            }
            cluster.origin_indexes.push_back(meshlet_vertices[meshlet.vertex_offset + meshlet_triangles[meshlet.triangle_offset + j * 3 + 0]]);

            auto pos = cluster.m_positions[cluster.m_indexes[j * 3 + 0]];
            auto origin_pos = InputMeshData.Vertices.Positions[cluster.origin_indexes.back()];
            
            
            cluster.origin_indexes.push_back(meshlet_vertices[meshlet.vertex_offset + meshlet_triangles[meshlet.triangle_offset + j * 3 + 1]]);
            cluster.origin_indexes.push_back(meshlet_vertices[meshlet.vertex_offset + meshlet_triangles[meshlet.triangle_offset + j * 3 + 2]]);
        }

       

        // Calculate bounding box for the cluster
        cluster.m_bounding_box = BBox();
        for (const auto& pos : cluster.m_positions) {
            cluster.m_bounding_box.unite(pos);
        }

        // Set min and max positions
        cluster.m_min_pos = glm::vec3(1e30f);
        cluster.m_max_pos = glm::vec3(-1e30f);
        for (const auto& pos : cluster.m_positions) {
            cluster.m_min_pos = glm::min(cluster.m_min_pos, pos);
            cluster.m_max_pos = glm::max(cluster.m_max_pos, pos);
        }

        // Insert the new cluster at the end of the existing clusters
        clusters.push_back(cluster);
    }

    // Optionally, you can log the number of clusters created
    LOGI("Created {} clusters from mesh optimization.", clusters.size() - initialClusterCount);

    std::span<Cluster> clusterSpan(clusters.data() + initialClusterCount, clusters.size() - initialClusterCount);
    //Build cluster adjancy

    auto sum_triangle = std::accumulate(clusterSpan.begin(), clusterSpan.end(), 0, [](int sum, const Cluster& cluster) { return sum + cluster.getTriangleCount(); });

    std::unordered_map<uint32_t, uint32_t> faceToCluster;
    size_t                                 globalFaceOffset = 0;
    for (size_t i = 0; i < clusterSpan.size(); i++) {
        for (size_t j = 0; j < clusterSpan[i].getTriangleCount(); j++) {
            if(globalFaceOffset + j == 835 / 3 ) {
                int k = 1;
            }
            faceToCluster[globalFaceOffset + j] = i + initialClusterCount;
        }
        // std::fill(faceToCluster
        clusterSpan[i].triangle_offset = globalFaceOffset;
        globalFaceOffset += clusterSpan[i].getTriangleCount();
    }
    if(false)
    {
        GraphAdjancy adjancy(sum_triangle * 3);
        PointHash    hash;

        

        globalFaceOffset = 0;
        for (auto& meshlet : clusterSpan) {
            for (size_t j = 0; j < meshlet.getTriangleCount(); ++j) {
                uint indexOffset = globalFaceOffset + j * 3;
                hash.ForAllMatchingPoint(indexOffset + 0, true, [&](uint32_t index) { return meshlet.m_positions[meshlet.m_indexes[index - globalFaceOffset]]; }, [&](uint32_t edgeIndex, uint32_t otherEdgeIndex) {
                    auto face0 = edgeIndex / 3;
                    auto face1 = otherEdgeIndex / 3;
                    if (faceToCluster[face0] != faceToCluster[face1]) {
                        adjancy.addEdge(edgeIndex, otherEdgeIndex);
                    } });
                hash.ForAllMatchingPoint(indexOffset + 1, true, [&](uint32_t index) { return meshlet.m_positions[meshlet.m_indexes[index - globalFaceOffset]]; }, [&](uint32_t edgeIndex, uint32_t otherEdgeIndex) {
                    auto face0 = edgeIndex / 3;
                    auto face1 = otherEdgeIndex / 3;
                    if (faceToCluster[face0] != faceToCluster[face1]) {
                        adjancy.addEdge(edgeIndex, otherEdgeIndex);
                    } });
                hash.ForAllMatchingPoint(indexOffset + 2, true, [&](uint32_t index) { return meshlet.m_positions[meshlet.m_indexes[index - globalFaceOffset]]; }, [&](uint32_t edgeIndex, uint32_t otherEdgeIndex) {
                    auto face0 = edgeIndex / 3;
                    auto face1 = otherEdgeIndex / 3;
                    if (faceToCluster[face0] != faceToCluster[face1]) {
                        adjancy.addEdge(edgeIndex, otherEdgeIndex);
                    } });
            }
            globalFaceOffset += meshlet.getTriangleCount() * 3;
        }
        int index = 0;
        int count  = 0;
        for (auto& adj : adjancy.adjVertices) {
            auto face         = index / 3;
            auto clusterIndex = faceToCluster[face];
            Triangle tri;
            tri.position[0] = clusterSpan[clusterIndex].m_positions[clusterSpan[clusterIndex].m_indexes[index/3 * 3 -  clusterSpan[clusterIndex].triangle_offset *3 ]];
            tri.position[1] = clusterSpan[clusterIndex].m_positions[clusterSpan[clusterIndex].m_indexes[index /3 * 3-   clusterSpan[clusterIndex].triangle_offset *3 + 1]];
            tri.position[2] = clusterSpan[clusterIndex].m_positions[clusterSpan[clusterIndex].m_indexes[index/3 * 3 -   clusterSpan[clusterIndex].triangle_offset *3+ 2]];

            int index_ = 0;
            for (auto& adjIndex : adj.adjVertices) {
                index_++;
                auto anotherFace         = adjIndex / 3;
                auto anotherClusterIndex = faceToCluster[anotherFace];
                Triangle anotherTri;
                anotherTri.position[0] = clusterSpan[anotherClusterIndex].m_positions[clusterSpan[anotherClusterIndex].m_indexes[adjIndex/3 * 3 -  clusterSpan[anotherClusterIndex].triangle_offset*3]];
                anotherTri.position[1] = clusterSpan[anotherClusterIndex].m_positions[clusterSpan[anotherClusterIndex].m_indexes[adjIndex/3 * 3 -  clusterSpan[anotherClusterIndex].triangle_offset*3 + 1]];
                anotherTri.position[2] = clusterSpan[anotherClusterIndex].m_positions[clusterSpan[anotherClusterIndex].m_indexes[adjIndex /3 * 3 - clusterSpan[anotherClusterIndex].triangle_offset*3 + 2]];

                auto hash_ = HashPosition(tri.position[index % 3]);
                auto & adj1 = hash.debugPositionTable[hash_];
            
                bool c = isTriangleAdjancy(tri, anotherTri);
                if (!c) {
                    LOGE("Triangle {} and {} is not adjancy", face, anotherFace);
                }
            
                if (clusterIndex != anotherClusterIndex) {
                    clusters[clusterIndex].m_linked_cluster.insert(anotherClusterIndex);
                    if(clusters[clusterIndex].m_linked_cluster_cost.contains(anotherClusterIndex)) {
                        clusters[clusterIndex].m_linked_cluster_cost[anotherClusterIndex] += 1;
                    }
                    else {
                        clusters[clusterIndex].m_linked_cluster_cost[anotherClusterIndex] = 1;
                    }
                    clusters[clusterIndex].m_linked_cluster_vec.push_back(anotherClusterIndex);
                    count++;
                }
            }
            index++;
        }

        for(auto & cluster : clusters) {
            LOGI("Cluster {} has {} linked clusters", cluster.guid, cluster.m_linked_cluster.size());
        }
        LOGI("Total adjancy count: {}", count);
    }

    {
        GraphAdjancy adjancy(sum_triangle * 3);
        adjancy.init(InputMeshData.TriangleIndices.size());
        for(auto& cluster : clusters) {
            for(auto i = 0;i<cluster.getTriangleCount();i++) {
                uint face_id = cluster.triangle_offset + i;
                glm::uvec3 tri = {cluster.origin_indexes[i * 3], cluster.origin_indexes[i * 3 + 1], cluster.origin_indexes[i * 3 + 2]};
                adjancy.add_edge(tri.x, tri.y,face_id);
                adjancy.add_edge(tri.y, tri.z,face_id);
                adjancy.add_edge(tri.z, tri.x,face_id);
            }
        }

        for (auto& edge : adjancy.adj_list) {
            auto from = &edge - adjancy.adj_list.data();
            for (auto [to, face0] : edge) {
                auto reverse_edge = adjancy.adj_list[to];
                if (reverse_edge.find(from) != reverse_edge.end()) {
                    uint face1 = reverse_edge[from];
                    auto mid1 = faceToCluster.find(face0)->second;
                    auto mid2 = faceToCluster.find(face1)->second;
                    if (mid1 != mid2) {
                        clusters[mid1].m_linked_cluster.insert(mid2);
                        if(clusters[mid1].m_linked_cluster_cost.contains(mid2)) {
                            clusters[mid1].m_linked_cluster_cost[mid2] += 1;
                        }
                        else {
                            clusters[mid1].m_linked_cluster_cost[mid2] = 1;
                        }
                        clusters[mid1].m_linked_cluster_vec.push_back(mid2);

                        clusters[mid2].m_linked_cluster.insert(mid1);
                        if(clusters[mid2].m_linked_cluster_cost.contains(mid1)) {
                            clusters[mid2].m_linked_cluster_cost[mid1] += 1;
                        }
                        else {
                            clusters[mid2].m_linked_cluster_cost[mid1] = 1;
                        }
                    }
                }
		
            }
        }
    }
}

void clusterTriangles1(std::vector<Cluster>& clusters, MeshInputData& InputMeshData, uint32_t baseTriangle, uint32_t numTriangles) {

    GraphAdjancy graphData          = buildAdjancy(std::span<uint32_t>(InputMeshData.TriangleIndices.data() + baseTriangle, numTriangles * 3), [&](uint32_t index) {
        return InputMeshData.Vertices.Positions[InputMeshData.TriangleIndices[index]];
    });
    uint32_t     targetClusterCount = numTriangles / ClusterSize;

    if (targetClusterCount == 1) {
        clusters.push_back(InitClusterFromMeshInputData(InputMeshData, baseTriangle, numTriangles));
        return;
    }

    GraphPartitioner partitioner(numTriangles, targetClusterCount);
    auto             graph = partitioner.NewGraph(numTriangles * 3);
    for (uint32_t i = 0; i < numTriangles; i++) {
        graph->AdjacencyOffset[i] = graph->Adjacency.size();
        for (int k = 0; k < 3; k++) {
            graphData.forAll(3 * i + k, [&](uint32_t edgeIndex0, uint32_t edgeIndex1) {
                partitioner.addAdjacency(graph, edgeIndex1 / 3, 4 * 65);
            });
        }
    }
    graph->AdjacencyOffset[numTriangles] = graph->Adjacency.size();

    checkTriangleAdjancy(*graph, InputMeshData.TriangleIndices, [&](uint32_t index) { return InputMeshData.Vertices.Positions[InputMeshData.TriangleIndices[index]]; });

    SaveMeshInputDataToObj(InputMeshData, FileUtils::getFilePath("mesh", "obj"));

    partitioner.partition(*graph);

    struct ClusterAdjacency {
        std::unordered_map<uint32_t, std::unordered_set<uint32_t>> adj_clusters;
        std::unordered_map<uint32_t, std::unordered_map<uint32_t, uint32_t>> adj_clusters_cost;

        void addEdge(uint32_t cluster1, uint32_t cluster2) {
            if (cluster1 != cluster2) {
                adj_clusters[cluster1].insert(cluster2);
                adj_clusters[cluster2].insert(cluster1);
                if(adj_clusters_cost[cluster1].contains(cluster2)) {
                    adj_clusters_cost[cluster1][cluster2] += 1;
                }
                else {
                    adj_clusters_cost[cluster1][cluster2] = 1;
                }
                if(adj_clusters_cost[cluster2].contains(cluster1)) {
                    adj_clusters_cost[cluster2][cluster1] += 1;
                }
                else {
                    adj_clusters_cost[cluster2][cluster1] = 1;
                }
            }
        }
    } clusterAdj;

    std::vector<std::unordered_map<uint32_t, uint32_t>> oldToNewIndexMaps(targetClusterCount);
    const uint32_t                                      oldClusterCount = clusters.size();
    clusters.resize(clusters.size() + targetClusterCount);

    for (uint32_t i = 0; i < numTriangles; i++) {
        int   partId           = partitioner.partitionIDs[i];
        auto  clusterId        = oldClusterCount + partId;
        auto& oldToNewIndexMap = oldToNewIndexMaps[partId];

        for (int k = 0; k < 3; k++) {
            uint32_t globalIndex = InputMeshData.TriangleIndices[baseTriangle + i * 3 + k];

            // if (oldToNewIndexMap.find(globalIndex) == oldToNewIndexMap.end()) {
            //     oldToNewIndexMap[globalIndex] = clusters[clusterId].m_positions.size();
            clusters[clusterId].m_positions.push_back(InputMeshData.Vertices.Positions[globalIndex]);
            clusters[clusterId].m_normals.push_back(InputMeshData.Vertices.Normals[globalIndex]);
            clusters[clusterId].m_uvs.push_back(InputMeshData.Vertices.UVs[globalIndex]);
            //}
            clusters[clusterId].m_indexes.push_back(clusters[clusterId].m_positions.size() - 1);
        }
    }

    // for(int i =oldClusterCount; i < clusters.size(); i++) {
    //     int clusterId = i;
    //     if(!clusters[clusterId].isConnected()) {
    //         saveSignleClusterToObj(clusters[clusterId], FileUtils::getFilePath("disconnected_cluster", "obj"));
    //         LOGE("Cluster {} is not connected", clusterId);
    //     }
    // }

    for (uint32_t i = 0; i < numTriangles * 3; i++) {
        uint32_t currentCluster = partitioner.partitionIDs[i / 3] + oldClusterCount;

        graphData.forAll(i, [&](uint32_t edgeIndex0, uint32_t edgeIndex1) {
            uint32_t neighborCluster = partitioner.partitionIDs[edgeIndex1 / 3] + oldClusterCount;
            if (currentCluster != neighborCluster) {
                clusterAdj.addEdge(currentCluster, neighborCluster);
            }
        });
    }

    for (const auto& [clusterId, adjClusters] : clusterAdj.adj_clusters) {
        auto& cluster            = clusters[clusterId];
        cluster.m_linked_cluster = adjClusters;
        cluster.m_linked_cluster_cost = clusterAdj.adj_clusters_cost[clusterId];

        for (auto adjClusterId : adjClusters) {
            const auto& adjCluster = clusters[adjClusterId];

            for (uint32_t i = 0; i < cluster.m_positions.size(); i++) {
                const auto& pos = cluster.m_positions[i];

                for (uint32_t j = 0; j < adjCluster.m_positions.size(); j++) {
                    if (pos == adjCluster.m_positions[j]) {
                        ClusterExternEdge edge;
                        edge.v0           = i;
                        edge.v1           = j;
                        edge.clusterIndex = adjClusterId;
                        cluster.m_external_edges.push_back(edge);
                        break;
                    }
                }
            }
        }
    }

    for (uint32_t i = oldClusterCount; i < clusters.size(); i++) {
        auto& cluster = clusters[i];
        cluster.guid  = i;

        cluster.m_bounding_box = BBox();
        cluster.m_min_pos      = glm::vec3(1e30f);
        cluster.m_max_pos      = glm::vec3(-1e30f);
        cluster.m_mip_level    = InputMeshData.mipLevel;

        for (const auto& pos : cluster.m_positions) {
            cluster.m_bounding_box.unite(pos);
            cluster.m_min_pos = glm::min(cluster.m_min_pos, pos);
            cluster.m_max_pos = glm::max(cluster.m_max_pos, pos);
        }
    }

    delete graph;
}

void clusterTriangles3(std::vector<Cluster>& clusters, MeshInputData& InputMeshData, uint32_t baseTriangle, uint32_t numTriangles) {
    if (numTriangles == 0) return;

    uint32_t         targetClusterCount = std::max(1u, (numTriangles + ClusterSize - 1) / ClusterSize);
    GraphPartitioner partitioner(numTriangles, targetClusterCount);
    auto             graph = partitioner.NewGraph(numTriangles * 6);

    struct Edge {
        uint32_t v0, v1;

        Edge(uint32_t a, uint32_t b) {
            v0 = std::min(a, b);
            v1 = std::max(a, b);
        }

        bool operator==(const Edge& other) const {
            return (v0 == other.v0 && v1 == other.v1);
        }
    };

    struct EdgeHash {
        size_t operator()(const Edge& edge) const {
            return (static_cast<size_t>(edge.v0) << 32) | edge.v1;
        }
    };

    std::unordered_map<Edge, std::vector<uint32_t>, EdgeHash> edgeToTriangles;
    edgeToTriangles.reserve(numTriangles * 3);

    for (uint32_t triIdx = 0; triIdx < numTriangles; triIdx++) {
        uint32_t idx = baseTriangle + triIdx * 3;
        uint32_t i0  = InputMeshData.TriangleIndices[idx + 0];
        uint32_t i1  = InputMeshData.TriangleIndices[idx + 1];
        uint32_t i2  = InputMeshData.TriangleIndices[idx + 2];

        edgeToTriangles[Edge(i0, i1)].push_back(triIdx);
        edgeToTriangles[Edge(i1, i2)].push_back(triIdx);
        edgeToTriangles[Edge(i2, i0)].push_back(triIdx);
    }

    std::vector<std::vector<std::pair<uint32_t, float>>> triangleAdjacency(numTriangles);

    for (const auto& pair : edgeToTriangles) {
        const auto& triangles = pair.second;
        for (size_t i = 0; i < triangles.size(); ++i) {
            for (size_t j = i + 1; j < triangles.size(); ++j) {
                triangleAdjacency[triangles[i]].push_back(std::make_pair(triangles[j], 1.0f));
                triangleAdjacency[triangles[j]].push_back(std::make_pair(triangles[i], 1.0f));
            }
        }
    }

    uint32_t currentOffset    = 0;
    graph->AdjacencyOffset[0] = 0;

    for (uint32_t i = 0; i < numTriangles; i++) {
        const auto& adjacentTris = triangleAdjacency[i];

        for (const auto& [adjTri, weight] : adjacentTris) {
            graph->Adjacency.push_back(adjTri);
            graph->AdjacencyCost.push_back(weight);
            currentOffset++;
        }

        graph->AdjacencyOffset[i + 1] = currentOffset;
    }

    partitioner.partition(*graph);

    const uint32_t oldClusterCount = clusters.size();
    clusters.resize(oldClusterCount + targetClusterCount);
    std::vector<std::unordered_map<uint32_t, uint32_t>> oldToNewIndexMaps(targetClusterCount);

    for (uint32_t i = 0; i < numTriangles; i++) {
        int   partId   = partitioner.partitionIDs[i];
        auto& cluster  = clusters[oldClusterCount + partId];
        auto& indexMap = oldToNewIndexMaps[partId];

        for (int k = 0; k < 3; k++) {
            uint32_t globalIndex = InputMeshData.TriangleIndices[baseTriangle + i * 3 + k];
            if (indexMap.find(globalIndex) == indexMap.end()) {
                indexMap[globalIndex] = cluster.m_positions.size();
                cluster.m_positions.push_back(InputMeshData.Vertices.Positions[globalIndex]);
                cluster.m_normals.push_back(InputMeshData.Vertices.Normals[globalIndex]);
                cluster.m_uvs.push_back(InputMeshData.Vertices.UVs[globalIndex]);
                cluster.m_bounding_box.unite(cluster.m_positions.back());
                cluster.m_min_pos = glm::min(cluster.m_min_pos, cluster.m_positions.back());
                cluster.m_max_pos = glm::max(cluster.m_max_pos, cluster.m_positions.back());
            }
            cluster.m_indexes.push_back(indexMap[globalIndex]);
        }
    }

    for (uint32_t i = 0; i < numTriangles; i++) {
        int   partId   = partitioner.partitionIDs[i];
        auto& cluster  = clusters[oldClusterCount + partId];
        auto& indexMap = oldToNewIndexMaps[partId];

        for (int k = 0; k < 3; k++) {
            uint32_t globalIndex = InputMeshData.TriangleIndices[baseTriangle + i * 3 + k];
            cluster.m_indexes.push_back(indexMap[globalIndex]);
        }
    }

    struct ClusterEdge {
        uint32_t v0, v1;
        ClusterEdge(uint32_t a, uint32_t b) : v0(std::min(a, b)),
                                              v1(std::max(a, b)) {}

        bool operator==(const ClusterEdge& other) const {
            return v0 == other.v0 && v1 == other.v1;
        }
    };

    struct ClusterEdgeHash {
        size_t operator()(const ClusterEdge& edge) const {
            return (static_cast<size_t>(edge.v0) << 32) | edge.v1;
        }
    };

    std::unordered_map<ClusterEdge, std::vector<uint32_t>, ClusterEdgeHash> clusterEdges;

    for (uint32_t i = 0; i < numTriangles; i++) {
        int      partId    = partitioner.partitionIDs[i];
        uint32_t clusterId = oldClusterCount + partId;
        auto&    cluster   = clusters[clusterId];
        auto&    indexMap  = oldToNewIndexMaps[partId];

        uint32_t i0 = InputMeshData.TriangleIndices[baseTriangle + i * 3 + 0];
        uint32_t i1 = InputMeshData.TriangleIndices[baseTriangle + i * 3 + 1];
        uint32_t i2 = InputMeshData.TriangleIndices[baseTriangle + i * 3 + 2];

        clusterEdges[ClusterEdge(i0, i1)].push_back(clusterId);
        clusterEdges[ClusterEdge(i1, i2)].push_back(clusterId);
        clusterEdges[ClusterEdge(i2, i0)].push_back(clusterId);
    }

    for (const auto& [edge, clusterIds] : clusterEdges) {
        if (clusterIds.size() > 1) {
            for (size_t i = 0; i < clusterIds.size(); ++i) {
                uint32_t clusterId = clusterIds[i];
                auto&    cluster   = clusters[clusterId];
                auto&    indexMap  = oldToNewIndexMaps[clusterId - oldClusterCount];

                uint32_t localV0 = indexMap[edge.v0];
                uint32_t localV1 = indexMap[edge.v1];

                for (size_t j = 0; j < clusterIds.size(); ++j) {
                    if (i != j) {
                        cluster.m_external_edges.push_back({localV0,
                                                            localV1,
                                                            clusterIds[j]});
                    }
                }
            }
        }
    }

    for (uint32_t i = oldClusterCount; i < clusters.size(); ++i) {
        auto& cluster = clusters[i];

        for (const auto& pos : cluster.m_positions) {
            cluster.m_min_pos = glm::min(cluster.m_min_pos, pos);
            cluster.m_max_pos = glm::max(cluster.m_max_pos, pos);
            cluster.m_bounding_box.unite(pos);
        }

        cluster.guid = i;

        for (const auto& edge : cluster.m_external_edges) {
            cluster.m_linked_cluster.insert(edge.clusterIndex);
        }
    }

    delete graph;
}

inline float calculateEdgeWeight(const glm::vec3& pos1, const glm::vec3& pos2, const glm::vec3& normal1, const glm::vec3& normal2) {
    float distWeight   = glm::length2(pos1 - pos2);
    float normalWeight = 1.0f - glm::dot(normal1, normal2);
    return distWeight * (1.0f + normalWeight * 2.0f);
}

void clusterTriangles(std::vector<Cluster>& clusters, MeshInputData& InputMeshData, uint32_t baseTriangle, uint32_t numTriangles) {
}

// void BuildDAG(std::vector<ClusterGroup>& groups, std::vector<Cluster>& clusters, uint32_t ClusterStart, uint32_t clusterRangeNum, uint32_t MeshIndex, BBox MeshBounds) {
//     bool bFirstLevel = true;
//     std::atomic<uint32_t> numClusters = 0;
//     uint32_t levelOffset = ClusterStart;
//
//     while (true) {
//         std::span<Cluster> levelClusters(&clusters[levelOffset], bFirstLevel ? clusterRangeNum : clusters.size() - levelOffset);
//         bFirstLevel = false;
//
//         if (levelClusters.size() < 2) {
//             break;
//         }
//
//         if (levelClusters.size() <= MaxClusterGroupSize) {
//             std::vector<uint32_t> children;
//             uint32_t numGroupElements = 0;
//             for (uint32_t i = 0; i < levelClusters.size(); i++) {
//                 children.push_back(levelOffset + i);
//                 numGroupElements += levelClusters[i].m_indexes.size();
//             }
//             uint32_t maxParents = numGroupElements / (ClusterSize * 2);
//             DAGReduce(groups, clusters, numClusters, children, maxParents, groups.size() - 1, MeshIndex);
//         } else {
//             GraphAdjancy adjancy = buildClusterGroupAdjancy(levelClusters);
//
//             uint32_t targetGroupCount = (levelClusters.size() + MinClusterGroupSize - 1) / MinClusterGroupSize;
//             GraphPartitioner partitioner(levelClusters.size(), targetGroupCount);
//
//             auto graph = partitioner.NewGraph(levelClusters.size() * 6);
//             if (!graph) {
//                 LOGE("Failed to create graph for cluster grouping");
//                 return;
//             }
//
//             graph->AdjacencyOffset[0] = 0;
//             for (uint32_t i = 0; i < levelClusters.size(); i++) {
//                 graph->AdjacencyOffset[i + 1] = graph->Adjacency.size();
//                 for (const auto& adjClusterId : levelClusters[i].m_linked_cluster) {
//                     if (adjClusterId >= levelOffset && adjClusterId < levelOffset + levelClusters.size()) {
//                         float weight = 1.0f;
//                         partitioner.addAdjacency(graph, adjClusterId - levelOffset, weight);
//                     }
//                 }
//             }
//
//             partitioner.partition(*graph);
//             delete graph;
//
//             groups.resize(groups.size() + partitioner.ranges.size());
//             for (auto& Range : partitioner.ranges) {
//                 std::vector<uint32_t> children;
//                 uint32_t numGroupElements = 0;
//
//                 for (uint32_t i = Range.start; i < Range.end; i++) {
//                     uint32_t clusterId = levelOffset + partitioner.indexes[i];
//                     children.push_back(clusterId);
//                     numGroupElements += clusters[clusterId].m_indexes.size();
//                 }
//
//                 uint32_t maxParents = numGroupElements / (ClusterSize * 2);
//                 uint32_t clusterGroupIndex = groups.size() - partitioner.ranges.size();
//                 DAGReduce(groups, clusters, numClusters, children, maxParents, clusterGroupIndex, MeshIndex);
//             }
//         }
//
//         levelOffset = clusters.size() - numClusters;
//     }
// }

static uint32_t MaxClusterGroupSize = 32;
static uint32_t MinClusterGroupSize = 8;

Cluster::Cluster(std::vector<Cluster*>& clusters) {
    const uint32_t NumTrisGuess = clusters.size() * 128;
    m_positions.reserve(NumTrisGuess * 3);
    m_normals.reserve(NumTrisGuess * 3);
    m_uvs.reserve(NumTrisGuess * 3);
    m_indexes.reserve(NumTrisGuess * 3);
    m_external_edges.clear();

    for (Cluster* cluster : clusters) {
        for (uint32_t i = 0; i < cluster->m_positions.size(); i++) {
            m_positions.push_back(cluster->m_positions[i]);
            m_normals.push_back(cluster->m_normals[i]);
            m_uvs.push_back(cluster->m_uvs[i]);
            m_indexes.push_back(m_positions.size() - 1);
        }
        m_bounding_box = m_bounding_box + cluster->m_bounding_box;
        m_min_pos      = glm::min(m_min_pos, cluster->m_min_pos);
        m_max_pos      = glm::max(m_max_pos, cluster->m_max_pos);
    }

    GraphAdjancy adjancy;

    uint32_t minIndex = 0;
    uint32_t maxIndex = clusters[0]->m_external_edges.size();
    for (auto edgeIndex = 0; edgeIndex < m_external_edges.size(); edgeIndex++) {
        if (edgeIndex >= maxIndex) {
            minIndex = maxIndex;
            maxIndex += clusters[edgeIndex]->m_external_edges.size();
        }
        auto adjCount = m_external_edges[edgeIndex].adjCount;
        adjancy.forAll(edgeIndex, [&](uint32_t edgeIndex, uint32_t adjIndex) {
            if (adjIndex < minIndex || adjIndex >= maxIndex) {
                adjCount++;
            }
        });
        adjCount                             = std::max(adjCount, 0u);
        m_external_edges[edgeIndex].adjCount = adjCount;
    }
}
Cluster::Cluster(Cluster* source, uint32_t start, uint32_t end, std::vector<uint32_t>& indexes, GraphAdjancy& adjancy) {
    m_positions.resize((end - start) * 3);
    m_normals.resize((end - start) * 3);
    m_uvs.resize((end - start) * 3);
    m_indexes.resize((end - start) * 3);
    m_external_edges.clear();
    m_bounding_box = BBox();
    m_min_pos      = glm::vec3(1e30f, 1e30f, 1e30f);
    m_max_pos      = glm::vec3(-1e30f, -1e30f, -1e30f);
    for (uint32_t i = start; i < end; i++) {
        uint32_t index         = indexes[i];
        m_positions[i - start] = source->m_positions[index];
        m_normals[i - start]   = source->m_normals[index];
        m_uvs[i - start]       = source->m_uvs[index];
        m_indexes[i - start]   = source->m_indexes[index];
        m_bounding_box         = m_bounding_box + m_positions[i - start];
        m_min_pos              = glm::min(m_min_pos, m_positions[i - start]);
        m_max_pos              = glm::max(m_max_pos, m_positions[i - start]);
    }
    for (uint32_t i = 0; i < m_indexes.size(); i++) {
        uint32_t index = m_indexes[i];
        for (uint32_t adjIndex : adjancy.adjVertices[index].adjVertices) {
            if (adjIndex < start || adjIndex >= end) {
            }
        }
    }
}
GraphAdjancy Cluster::buildAdjacency() {
    return GraphAdjancy();
}

// ClusterGroup 结构体定义
struct ClusterGroup {
    // uint32_t startIndex; // 在clusters数组中的起始索引
    // uint32_t count;      // 包含的cluster数量
    std::vector<uint32_t> clusterIndexes{};
    BBox                  boundingBox;// 组的包围盒
    float                 errorMetric;// LOD误差度量
    uint                  lodLevel = 0;
    ClusterGroup()
        : boundingBox(), errorMetric(0.0f) {}
};

float simplifyMeshData1(MeshInputData& inputData, uint32_t targetNumTris) {
    if (inputData.TriangleIndices.empty() || targetNumTris >= inputData.TriangleIndices.size() / 3) {
        return 0.0f;
    }
    // SaveMeshInputDataToObj(inputData, "before.obj");

    // 准备顶点位置数据
    size_t             vertexCount = inputData.Vertices.Positions.size();
    std::vector<float> vertex_positions;
    vertex_positions.reserve(vertexCount * 3);
    for (const auto& pos : inputData.Vertices.Positions) {
        vertex_positions.push_back(pos.x);
        vertex_positions.push_back(pos.y);
        vertex_positions.push_back(pos.z);
    }

    // 准备顶点属性数据 (normals + uvs)
    std::vector<float> vertex_attributes;
    vertex_attributes.reserve(vertexCount * (3 + 2));// 3 for normal, 2 for uv
    for (size_t i = 0; i < vertexCount; ++i) {
        // Add normal
        vertex_attributes.push_back(inputData.Vertices.Normals[i].x);
        vertex_attributes.push_back(inputData.Vertices.Normals[i].y);
        vertex_attributes.push_back(inputData.Vertices.Normals[i].z);
        // Add UV
        vertex_attributes.push_back(inputData.Vertices.UVs[i].x);
        vertex_attributes.push_back(inputData.Vertices.UVs[i].y);
    }

    // 设置属性权重
    std::vector<float> attribute_weights = {1.0f, 1.0f};// normal和uv的权重

    // 准备索引数据
    std::vector<unsigned int> indices(inputData.TriangleIndices.begin(), inputData.TriangleIndices.end());
    std::vector<unsigned int> destination_indices(indices.size());

    // 简化参数
    float target_error = 0.1f;
    float lod_error    = 0.0f;
    uint  max_iter     = 10;
    uint  iteration    = 0;

    size_t simplified_index_count = 0;
    do {
        simplified_index_count = meshopt_simplifyWithAttributes(
            destination_indices.data(),// destination indices
            indices.data(),            // source indices
            indices.size(),            // index count
            vertex_positions.data(),   // vertex positions
            vertexCount,               // vertex count
            sizeof(float) * 3,         // vertex position stride
            vertex_attributes.data(),  // vertex attributes
            sizeof(float) * 5,         // vertex attribute stride (3 for normal + 2 for uv)
            attribute_weights.data(),  // attribute weights
            2,                         // attribute count (normal and uv)
            nullptr,                   // vertex lock (optional)
            targetNumTris * 3,         // target index count
            target_error,              // target error
            0,                         // options
            &lod_error                 // result error
        );

        target_error *= 1.5f;
        target_error = std::min(target_error, 3.0f);
        iteration++;
    } while (float(simplified_index_count) > float(targetNumTris * 3) * 1.1f && iteration < max_iter);

    if (iteration >= max_iter) {
        LOGE("simplify iteration exceed max iteration {}", max_iter);
        return 0.0f;
    }

    // 重新映射顶点
    std::vector<unsigned int> remap(vertexCount);
    size_t                    unique_vertex_count = meshopt_generateVertexRemap(
        remap.data(),
        destination_indices.data(),
        simplified_index_count,
        vertex_positions.data(),
        vertexCount,
        sizeof(float) * 3);

    if (unique_vertex_count == 0) {
        LOGE("meshopt_generateVertexRemap failed");
        return 0.0f;
    }

    // 创建新的顶点数据
    std::vector<glm::vec3> new_positions;
    std::vector<glm::vec3> new_normals;
    std::vector<glm::vec2> new_uvs;
    new_positions.reserve(unique_vertex_count);
    new_normals.reserve(unique_vertex_count);
    new_uvs.reserve(unique_vertex_count);

    // 创建重映射后的顶点缓冲区
    std::vector<unsigned int> remapped_indices(simplified_index_count);
    meshopt_remapIndexBuffer(remapped_indices.data(), destination_indices.data(), simplified_index_count, remap.data());

    // 重新映射顶点属性
    std::vector<float> remapped_vertices(unique_vertex_count * 3);
    std::vector<float> remapped_attributes(unique_vertex_count * 5);

    meshopt_remapVertexBuffer(remapped_vertices.data(), vertex_positions.data(), vertexCount, sizeof(float) * 3, remap.data());
    meshopt_remapVertexBuffer(remapped_attributes.data(), vertex_attributes.data(), vertexCount, sizeof(float) * 5, remap.data());

    // 转换回glm类型
    for (size_t i = 0; i < unique_vertex_count; ++i) {
        new_positions.push_back(glm::vec3(
            remapped_vertices[i * 3 + 0],
            remapped_vertices[i * 3 + 1],
            remapped_vertices[i * 3 + 2]));
        new_normals.push_back(glm::vec3(
            remapped_attributes[i * 5 + 0],
            remapped_attributes[i * 5 + 1],
            remapped_attributes[i * 5 + 2]));
        new_uvs.push_back(glm::vec2(
            remapped_attributes[i * 5 + 3],
            remapped_attributes[i * 5 + 4]));
    }

    // 更新输入数据
    inputData.Vertices.Positions = std::move(new_positions);
    inputData.Vertices.Normals   = std::move(new_normals);
    inputData.Vertices.UVs       = std::move(new_uvs);

    // 更新索引
    inputData.TriangleIndices = std::move(remapped_indices);

    // 更新三角形计数
    inputData.TriangleCounts[0] = simplified_index_count / 3;

    SaveMeshInputDataToObj(inputData, "after.obj");
    return lod_error;
}

// 在适当位置添加以下函数实现
uint32_t CreateBVHNode(std::vector<BVHNode>& nodes) {
    nodes.push_back(BVHNode());
    return nodes.size() - 1;
}

BBox CalculateClusterBounds(const Cluster& cluster) {
    BBox bounds;
    for (const auto& pos : cluster.m_positions) {
        bounds = bounds + pos;
    }
    return bounds;
}

// 计算BVH成本的辅助函数
float BVH_Cost(const std::vector<BVHNode>& nodes, std::span<const uint32_t> nodeIndices) {
    if (nodeIndices.empty()) return 0.0f;

    BBox bound = nodes[nodeIndices[0]].bounds;
    for (size_t i = 1; i < nodeIndices.size(); ++i) {
        bound = bound + nodes[nodeIndices[i]].bounds;
    }

    glm::vec3 extent = bound.max() - bound.min();
    return extent.x + extent.y + extent.z;
}

void BVH_SortNodes(const std::vector<BVHNode>& Nodes, std::span<uint32_t> NodeIndices, const std::vector<uint32_t>& ChildSizes) {
    // 执行NANITE_MAX_BVH_NODE_FANOUT_BITS次二分分割
    for (uint32_t Level = 0; Level < NANITE_MAX_BVH_NODE_FANOUT_BITS; Level++) {
        const uint32_t NumBuckets               = 1 << Level;
        const uint32_t NumChildrenPerBucket     = NANITE_MAX_BVH_NODE_FANOUT >> Level;
        const uint32_t NumChildrenPerBucketHalf = NumChildrenPerBucket >> 1;

        uint32_t BucketStartIndex = 0;
        for (uint32_t BucketIndex = 0; BucketIndex < NumBuckets; BucketIndex++) {
            const uint32_t FirstChild = NumChildrenPerBucket * BucketIndex;

            uint32_t Sizes[2] = {0, 0};
            for (uint32_t i = 0; i < NumChildrenPerBucketHalf; i++) {
                Sizes[0] += ChildSizes[FirstChild + i];
                Sizes[1] += ChildSizes[FirstChild + i + NumChildrenPerBucketHalf];
            }

            auto NodeIndices01 = NodeIndices.subspan(BucketStartIndex, Sizes[0] + Sizes[1]);
            auto NodeIndices0  = NodeIndices.subspan(BucketStartIndex, Sizes[0]);
            auto NodeIndices1  = NodeIndices.subspan(BucketStartIndex + Sizes[0], Sizes[1]);

            BucketStartIndex += Sizes[0] + Sizes[1];

            auto SortByAxis = [&](uint32_t AxisIndex) {
                auto compareFunc = [&Nodes, AxisIndex](uint32_t A, uint32_t B) {
                    glm::vec3 centerA = (Nodes[A].bounds.min() + Nodes[A].bounds.max()) * 0.5f;
                    glm::vec3 centerB = (Nodes[B].bounds.min() + Nodes[B].bounds.max()) * 0.5f;
                    return centerA[AxisIndex] < centerB[AxisIndex];
                };

                std::sort(NodeIndices01.begin(), NodeIndices01.end(), compareFunc);
            };

            float    BestCost      = MAX_FLT;
            uint32_t BestAxisIndex = 0;

            // 尝试沿不同轴排序并选择最佳的
            const uint32_t NumAxes = 3;
            for (uint32_t AxisIndex = 0; AxisIndex < NumAxes; AxisIndex++) {
                SortByAxis(AxisIndex);

                float Cost = BVH_Cost(Nodes, NodeIndices0) + BVH_Cost(Nodes, NodeIndices1);
                if (Cost < BestCost) {
                    BestCost      = Cost;
                    BestAxisIndex = AxisIndex;
                }
            }

            // 如果最佳轴不是最后一个，则重新排序
            if (BestAxisIndex != NumAxes - 1) {
                SortByAxis(BestAxisIndex);
            }
        }
    }
}

uint32_t BuildBVHTopDown(std::vector<BVHNode>& nodes, std::span<uint32_t> indices, bool bSort) {
    const uint32_t numNode = indices.size();
    if (numNode == 1) {
        return indices[0];
    }

    auto&    node      = nodes.emplace_back();
    uint32_t nodeIndex = nodes.size() - 1;

    if (indices.size() <= 4) {
        node.children = std::vector<uint32_t>(indices.begin(), indices.end());
        return nodeIndex;
    }

    uint32_t TopSize = NANITE_MAX_BVH_NODE_FANOUT;
    while (TopSize * NANITE_MAX_BVH_NODE_FANOUT <= numNode) {
        TopSize *= NANITE_MAX_BVH_NODE_FANOUT;
    }

    const uint32_t LargeChildSize    = TopSize;
    const uint32_t SmallChildSize    = TopSize / NANITE_MAX_BVH_NODE_FANOUT;
    const uint32_t MaxExcessPerChild = LargeChildSize - SmallChildSize;

    std::vector<uint32_t> ChildSizes(NANITE_MAX_BVH_NODE_FANOUT);

    uint32_t Excess = numNode - TopSize;
    for (int32_t i = NANITE_MAX_BVH_NODE_FANOUT - 1; i >= 0; i--) {
        const uint32_t ChildExcess = std::min(Excess, MaxExcessPerChild);
        ChildSizes[i]              = SmallChildSize + ChildExcess;
        if (ChildSizes[i] == 3722304989) {
            LOGE("error");
        }
        Excess -= ChildExcess;
    }
    assert(Excess == 0);

    if (bSort) {
        BVH_SortNodes(nodes, indices, ChildSizes);
    }

    uint32_t Offset = 0;
    for (uint32_t i = 0; i < NANITE_MAX_BVH_NODE_FANOUT; i++) {
        uint32_t ChildSize = ChildSizes[i];
        uint32_t NodeIndex = BuildBVHTopDown(nodes, indices.subspan(Offset, ChildSize), bSort);
        nodes[nodeIndex].children.push_back(NodeIndex);
        Offset += ChildSize;
    }

    return nodeIndex;
}

SphereBox CalculateSphereBox(const std::vector<SphereBox>& boxes) {
    return boxes[0];
    // SphereBox result;
    // for (const auto& box : boxes) {
    //     result = result + box;
    // }
    // return result;
}

uint32_t BuildBVHRecursive(std::vector<BVHNode>& nodes, std::vector<TNode>& tnodes, const std::vector<ClusterGroup> groups, uint32_t rootIndex, uint32_t depth) {
    auto childNum = nodes[rootIndex].children.size();

    uint32 TNodeIndex = tnodes.size();
    auto&  tnode      = tnodes.emplace_back();
    for (uint32_t i = 0; i < childNum; i++) {
        uint32_t childIndex = nodes[rootIndex].children[i];
        if (nodes[childIndex].isLeaf) {
            tnode.ClusterGroupPartIndex[childIndex] = nodes[childIndex].groupIndex;
        } else {
            auto                   childTNodeIndex = BuildBVHRecursive(nodes, tnodes, groups, childIndex, depth + 1);
            auto                   childTNode      = tnodes[childTNodeIndex];
            std::vector<SphereBox> sphereBoxes;
            BBox                   box;
            float                  minLodError = FLT_MAX;
            float                  maxLodError = 0.0f;
            for (uint32 grandChildIndex : childTNode.ClusterGroupPartIndex) {
                auto& group = groups[grandChildIndex];
                box         = box + group.boundingBox;
                sphereBoxes.push_back(childTNode.LODBounds[grandChildIndex]);
                minLodError = std::min(minLodError, group.errorMetric);
                maxLodError = std::max(maxLodError, group.errorMetric);
            }

            tnode.Bounds[childIndex]             = box;
            tnode.LODBounds[childIndex]          = CalculateSphereBox(sphereBoxes);
            tnode.MinLODError[childIndex]        = minLodError;
            tnode.MaxLODError[childIndex]        = maxLodError;
            tnode.ChildrenStartIndex[childIndex] = childTNodeIndex;
        }
    }
    return TNodeIndex;
}

NaniteBVH BuildBVH(const std::vector<Cluster>& clusters, const std::vector<ClusterGroup>& groups) {
    NaniteBVH bvh;

    // 按LOD级别对clusters进行分组
    // std::map<uint32_t, std::vector<uint32_t>> lodClusters;
    // for (uint32_t i = 0; i < clusters.size(); i++) {
    //     lodClusters[clusters[i].m_mip_level].push_back(i);
    //     if(clusters[i].m_mip_level>=10) {
    //         int k = 1;
    //     }
    // }

    std::vector<BVHNode> nodes(groups.size());
    uint                 maxMipLevel = 0;
    for (uint32_t i = 0; i < groups.size(); i++) {
        nodes[i].groupIndex = i;
        nodes[i].bounds     = groups[i].boundingBox;
        nodes[i].isLeaf     = true;
        nodes[i].lodLevel   = groups[i].lodLevel;
        maxMipLevel         = std::max(maxMipLevel, groups[i].lodLevel);
    }
    std::vector<std::vector<uint32_t>> nodesByMip(maxMipLevel + 1);

    for (uint32_t i = 0; i < groups.size(); i++) {
        nodesByMip[groups[i].lodLevel].push_back(i);
    }
    // for (const auto& [lodLevel, clusterIndices] : lodClusters) {
    //     maxMipLevel = std::max(maxMipLevel, lodLevel);
    // }

    std::vector<uint32_t> levelRoots;
    for (int i = 0; i <= maxMipLevel; i++) {
        if (nodesByMip[i].empty()) {
            continue;
        }
        auto nodeIndex = BuildBVHTopDown(nodes, nodesByMip[i], true);
        LOGI("i: {}, nodeIndex: {}", i, nodeIndex);
        levelRoots.push_back(nodeIndex);
    }
    LOGI("BVH node1 count: {}", nodes.size());
    auto root = BuildBVHTopDown(nodes, levelRoots, false);

    LOGI("BVH node count: {}", nodes.size());
    std::vector<TNode> tnodes;
    BuildBVHRecursive(nodes, tnodes, groups, root, 0);

    return bvh;
}

MeshInputData mergeClusterToMeshInputData(std::vector<Cluster>& clusters, std::span<uint32_t> children) {
    MeshInputData mergedInputData;

    // 创建顶点重映射表
    std::unordered_map<uint32_t, uint32_t> vertexRemap;
    uint32_t                               nextVertexIndex = 0;

    // 第遍：收集所有唯一顶点并建立重映射
    for (uint32_t childId : children) {
        const auto& cluster = clusters[childId];
        for (uint32_t i = 0; i < cluster.m_positions.size(); i++) {
            const auto& pos    = cluster.m_positions[i];
            const auto& normal = cluster.m_normals[i];
            const auto& uv     = cluster.m_uvs[i];

            // 使用位置作为唯一标识符
            uint64_t hash = HashPosition(pos);

            if (vertexRemap.find(hash) == vertexRemap.end()) {
                vertexRemap[hash] = nextVertexIndex++;
                mergedInputData.Vertices.Positions.push_back(pos);
                mergedInputData.Vertices.Normals.push_back(normal);
                mergedInputData.Vertices.UVs.push_back(uv);
            }
        }
    }

    // 第二遍：重建三角形索引
    for (uint32_t childId : children) {
        const auto& cluster = clusters[childId];
        for (uint32_t i = 0; i < cluster.m_indexes.size(); i += 3) {
            uint32_t idx0 = cluster.m_indexes[i];
            uint32_t idx1 = cluster.m_indexes[i + 1];
            uint32_t idx2 = cluster.m_indexes[i + 2];

            const auto& pos0 = cluster.m_positions[idx0];
            const auto& pos1 = cluster.m_positions[idx1];
            const auto& pos2 = cluster.m_positions[idx2];

            uint64_t hash0 = HashPosition(pos0);
            uint64_t hash1 = HashPosition(pos1);
            uint64_t hash2 = HashPosition(pos2);

            mergedInputData.TriangleIndices.push_back(vertexRemap[hash0]);
            mergedInputData.TriangleIndices.push_back(vertexRemap[hash1]);
            mergedInputData.TriangleIndices.push_back(vertexRemap[hash2]);
        }
    }

    mergedInputData.TriangleCounts.push_back(mergedInputData.TriangleIndices.size() / 3);
    mergedInputData.mipLevel = clusters[children[0]].m_mip_level + 1;

    return mergedInputData;
}

// DAGReduce 函数定义
void DAGReduce(std::vector<ClusterGroup>& groups, std::vector<Cluster>& clusters, std::atomic<uint32_t>& numClusters, std::span<uint32_t> children, uint32_t maxParents, uint32_t groupIndex, uint32_t MeshIndex) {
    auto     mergedInputData = mergeClusterToMeshInputData(clusters, children);
    uint32_t targetNumTris   = maxParents * ClusterSize;
    float    error           = simplifyMeshData1(mergedInputData, targetNumTris);

    // 使用clusterTriangles1重新划分
    std::vector<Cluster> newClusters;
    clusterTriangles1(newClusters, mergedInputData, 0, mergedInputData.TriangleCounts[0]);

    // 更新groups和clusters
    uint32_t newClusterStart = clusters.size();
    clusters.insert(clusters.end(), newClusters.begin(), newClusters.end());

    // 更新group信息
    ClusterGroup newGroup;
    newGroup.clusterIndexes = std::vector<uint32_t>(children.begin(), children.end());
    newGroup.lodLevel       = mergedInputData.mipLevel;
    groups[groupIndex]      = std::move(newGroup);
}

void BuildDAG(std::vector<ClusterGroup>& groups, std::vector<Cluster>& clusters, uint32_t ClusterStart, uint32_t clusterRangeNum, uint32_t MeshIndex, BBox MeshBounds) {
    bool                  bFirstLevel = true;
    std::atomic<uint32_t> numClusters = 0;
    uint32_t              levelOffset = ClusterStart;

    bool buildRoot = true;
    while (true) {
        numClusters = clusters.size();
        std::span<Cluster> levelClusters(&clusters[levelOffset], bFirstLevel ? clusterRangeNum : clusters.size() - levelOffset);
        bFirstLevel = false;

        if (levelClusters.size() < 2) {
            break;
        }

        if (levelClusters.size() <= MaxClusterGroupSize) {
            std::vector<uint32_t> children;
            uint32_t              numGroupElements = 0;
            for (uint32_t i = 0; i < levelClusters.size(); i++) {
                children.push_back(levelOffset + i);
                numGroupElements += levelClusters[i].m_indexes.size() / 3;
            }
            uint32_t maxParents = numGroupElements / (ClusterSize * 2);
            DAGReduce(groups, clusters, numClusters, children, maxParents, groups.size() - 1, MeshIndex);
        } else {
            // GraphAdjancy adjancy = buildClusterGroupAdjancy1(levelClusters, levelOffset);

            uint32_t         targetGroupCount = (levelClusters.size() + MinClusterGroupSize - 1) / MinClusterGroupSize;
            GraphPartitioner partitioner(levelClusters.size(), targetGroupCount);

            auto graph = partitioner.NewGraph(levelClusters.size() * 6);
            if (!graph) {
                LOGE("Failed to create graph for cluster grouping");
                return;
            }

            for (uint32_t i = 0; i < levelClusters.size(); i++) {
                graph->AdjacencyOffset[i] = graph->Adjacency.size();
                for (const auto& adjClusterId : levelClusters[i].m_linked_cluster) {
                    if (adjClusterId >= levelOffset && adjClusterId < levelOffset + levelClusters.size()) {
                       float weight = levelClusters[i].m_linked_cluster_cost[adjClusterId - levelOffset];
                       // float weight = 1.0f;
                        partitioner.addAdjacency(graph, adjClusterId - levelOffset, weight);
                    }
                }
            }
            graph->AdjacencyOffset[levelClusters.size()] = graph->Adjacency.size();

            partitioner.partition(*graph);
            delete graph;

            // 根据partition结果重组clusters
            std::unordered_map<int, std::vector<uint32_t>> partitionGroups;

            // 将clusters按照partition ID分组
            for (uint32_t i = 0; i < levelClusters.size(); i++) {
                int partitionId = partitioner.partitionIDs[i];
                partitionGroups[partitionId].push_back(levelOffset + i);
            }

            // 为每个partition创建新的group
            groups.resize(groups.size() + partitionGroups.size());
            uint32_t groupStartIndex = groups.size() - partitionGroups.size();

            // 处理每个partition组
            uint32_t groupIndex = 0;

            for (auto& [partitionId, children] : partitionGroups) {
                // 遍历每个集群
                for (size_t clusterIndex = 0; clusterIndex < children.size(); ++clusterIndex) {
                    // 获取当前集群的索引
                    const auto& clusterChildren = children[clusterIndex];

                    // 合并当前集群的输入数据
                    std::vector<uint32_t> clusterChildrenSpan = {clusterChildren};
                    auto                  mergedInputData     = mergeClusterToMeshInputData(clusters, clusterChildrenSpan);

                    // 保存合并后的输入数据到 OBJ 文件，文件名包含 clusterIndex 和 groupIndex
                    SaveMeshInputDataToObj(
                        mergedInputData,
                        FileUtils::getFilePath("mergedInputData_group_" + std::to_string(groupIndex) + "_cluster_" + std::to_string(clusterIndex), "obj", true));
                }

                auto groupmesh = mergeClusterToMeshInputData(clusters, children);
                SaveMeshInputDataToObj(groupmesh, FileUtils::getFilePath("group_" + std::to_string(groupIndex), "obj", true));

                groupIndex++;
            }

            exit(-1);

            for (auto& [partitionId, children] : partitionGroups) {
                // 计算组内所有元素数量
                uint32_t numGroupElements = 0;
                for (uint32_t clusterId : children) {
                    numGroupElements += clusters[clusterId].m_indexes.size() / 3;
                }

                if (buildRoot) {
                }
                // 计算最大父节点数量
                uint32_t maxParents = numGroupElements / (ClusterSize * 2);

                // 为这个partition创建新的group
                DAGReduce(groups, clusters, numClusters, std::span<uint32_t>(children.data(), children.size()), maxParents, groupStartIndex + groupIndex, MeshIndex);

                groupIndex++;
            }
        }
        buildRoot   = false;
        levelOffset = numClusters;
    }
}

uint jenkinsHash(uint a) {
    a = (a + 0x7ed55d16) + (a << 12);
    a = (a ^ 0xc761c23cu) ^ (a >> 19);
    a = (a + 0x165667b1) + (a << 5);
    a = (a + 0xd3a2646c) ^ (a << 9);
    a = (a + 0xfd7046c5) + (a << 3);
    a = (a ^ 0xb55a4f09u) ^ (a >> 16);
    return a;
}

vec3 pseudocolor(uint value) {
    uint h = jenkinsHash(value);
    return glm::vec3(glm::uvec3(h, h >> 8, h >> 16) & 0xffu) / 255.f;
}

GraphPartitioner::FGraphData* GraphPartitioner::NewGraph(uint32_t maxEdges) const {
    auto graph = new FGraphData();

    graph->AdjacencyOffset.resize(numElements + 1, 0);
    graph->Adjacency.reserve(maxEdges);
    graph->AdjacencyCost.reserve(maxEdges);

    return graph;
}

void GraphPartitioner::addAdjacency(FGraphData* graph, uint32_t toVertex, float weight) {
    if (!graph || toVertex >= numElements) {
        LOGE("Invalid parameters in addAdjacency");
        return;
    }

    graph->Adjacency.push_back(toVertex);
    graph->AdjacencyCost.push_back(weight);
}
void NaniteBuilder::Build(MeshInputData& InputMeshData, MeshOutputData* OutFallbackMeshData, const MeshNaniteSettings& Settings) {
    LOGI("NaniteBuilder::Build Vertex Count: {}", InputMeshData.Vertices.Positions.size());

    std::vector<Cluster>  clusters;
    std::vector<uint32_t> clusterPerMesh;
    {
        uint32_t baseTriangle = 0;
        for (uint32_t numTriangles : InputMeshData.TriangleCounts) {
            uint32_t clusterOffset = clusters.size();
            clusterTrianglesByMeshOpt(clusters, InputMeshData, baseTriangle, numTriangles);
            clusterPerMesh.push_back(clusters.size() - clusterOffset);
            baseTriangle += numTriangles;
        }
    }           

    int index = 0;
    for (auto& cluster : clusters) {
        if(index == 315) {
            int k = 1;
        }
        saveSignleClusterToObj(cluster, FileUtils::getFilePath("cluster_" + std::to_string(index++), "obj", true));
    }
    
    std::vector<uint32_t> children;
    for (uint32_t i = 0; i < clusters.size(); i++) {
        children.push_back(i);
    }
    auto mesh_input_data = mergeClusterToMeshInputData(clusters, children);
    SaveMeshInputDataToObj(mesh_input_data, "mergedInputData.obj");
    // exit(-1);

    std::vector<ClusterGroup> groups;
    BuildDAG(groups, clusters, 0, clusters.size(), 0, BBox());

    NaniteBVH bvh = BuildBVH(clusters, groups);
    LOGI("NaniteBuilder::Build Cluster Count: {} Group Count: {}", clusters.size(), groups.size());
    // ... 其余代码保持不变 ...
}

template<typename T1, typename T2>
void ConvertData(std::vector<T1>& data, std::vector<T2>& outData) {
    outData.resize(data.size() * sizeof(T1) / sizeof(T2));
    std::memcpy(outData.data(), data.data(), data.size() * sizeof(T1));
}

std::unique_ptr<MeshInputData> NaniteBuilder::createNaniteExampleMeshInputData() {
    auto primData      = PrimitiveLoader::loadPrimitive(FileUtils::getResourcePath("tiny_nanite/jinx-combined.obj"));
    auto meshInputData = std::make_unique<MeshInputData>();
    ConvertData(primData->buffers[POSITION_ATTRIBUTE_NAME], meshInputData->Vertices.Positions);
    ConvertData(primData->buffers[NORMAL_ATTRIBUTE_NAME], meshInputData->Vertices.Normals);
    ConvertData(primData->buffers[TEXCOORD_ATTRIBUTE_NAME], meshInputData->Vertices.UVs);
    ConvertData(primData->indexs, meshInputData->TriangleIndices);
    meshInputData->TriangleCounts.push_back(meshInputData->TriangleIndices.size() / 3);
    meshInputData->MaterialIndices = {0};
    return meshInputData;
}

glm::vec3 getClusterColor(idx_t clusterId) {
    const float goldenRatio = 0.618033988749895f;
    const float saturation  = 0.6f;
    const float value       = 0.95f;

    float hue = fmod(clusterId * goldenRatio, 1.0f);

    // HSV to RGB
    float h = hue * 6.0f;
    float c = value * saturation;
    float x = c * (1.0f - fabs(fmod(h, 2.0f) - 1.0f));
    float m = value - c;

    glm::vec3 rgb;
    if (h < 1.0f)
        rgb = glm::vec3(c, x, 0.0f);
    else if (h < 2.0f)
        rgb = glm::vec3(x, c, 0.0f);
    else if (h < 3.0f)
        rgb = glm::vec3(0.0f, c, x);
    else if (h < 4.0f)
        rgb = glm::vec3(0.0f, x, c);
    else if (h < 5.0f)
        rgb = glm::vec3(x, 0.0f, c);
    else
        rgb = glm::vec3(c, 0.0f, x);

    return rgb + glm::vec3(m);
}

glm::vec4 getClusterColorPalette(uint32_t clusterId) {
    static const glm::vec4 palette[] = {
        {0.957f, 0.263f, 0.212f, 1.0f},// Red
        {0.133f, 0.545f, 0.133f, 1.0f},// Green
        {0.231f, 0.455f, 0.969f, 1.0f},// Blue
        {0.945f, 0.769f, 0.059f, 1.0f},// Yellow
        {0.608f, 0.349f, 0.714f, 1.0f},// Purple
        {0.004f, 0.588f, 0.533f, 1.0f},// Teal
        {0.957f, 0.643f, 0.376f, 1.0f},// Orange
        {0.741f, 0.718f, 0.420f, 1.0f},// Olive
        {0.404f, 0.227f, 0.718f, 1.0f},// Indigo
        {0.914f, 0.118f, 0.388f, 1.0f},// Pink
        {0.475f, 0.333f, 0.282f, 1.0f},// Brown
        {0.612f, 0.153f, 0.690f, 1.0f},// Deep Purple
    };

    const size_t paletteSize = sizeof(palette) / sizeof(palette[0]);
    return palette[clusterId % paletteSize];
}

glm::vec3 getClusterColorCombined(uint32_t clusterId) {
    // 使用预定义调色板的基础颜色
    glm::vec4 baseColor = getClusterColorPalette(clusterId / 12);

    // 使用黄金比例法微调色相
    float hueShift = fmod(clusterId * 0.618033988749895f, 0.2f) - 0.1f;

    // RGB to HSV
    float maxVal = std::max(std::max(baseColor.r, baseColor.g), baseColor.b);
    float minVal = std::min(std::min(baseColor.r, baseColor.g), baseColor.b);
    float delta  = maxVal - minVal;

    float hue = 0.0f;
    if (delta > 0.0f) {
        if (maxVal == baseColor.r) {
            hue = fmod((baseColor.g - baseColor.b) / delta + 6.0f, 6.0f) / 6.0f;
        } else if (maxVal == baseColor.g) {
            hue = ((baseColor.b - baseColor.r) / delta + 2.0f) / 6.0f;
        } else {
            hue = ((baseColor.r - baseColor.g) / delta + 4.0f) / 6.0f;
        }
    }

    // 调整色相
    hue = fmod(hue + hueShift + 1.0f, 1.0f);

    // HSV to RGB
    float h = hue * 6.0f;
    float c = maxVal * delta;
    float x = c * (1.0f - fabs(fmod(h, 2.0f) - 1.0f));
    float m = maxVal - c;

    glm::vec3 rgb;
    if (h < 1.0f)
        rgb = glm::vec3(c, x, 0.0f);
    else if (h < 2.0f)
        rgb = glm::vec3(x, c, 0.0f);
    else if (h < 3.0f)
        rgb = glm::vec3(0.0f, c, x);
    else if (h < 4.0f)
        rgb = glm::vec3(0.0f, x, c);
    else if (h < 5.0f)
        rgb = glm::vec3(x, 0.0f, c);
    else
        rgb = glm::vec3(c, 0.0f, x);

    return rgb + glm::vec3(m);
}

std::unique_ptr<Primitive> NaniteBuilder::createNaniteExamplePrimitive() {
    auto primData      = PrimitiveLoader::loadPrimitive(FileUtils::getResourcePath("tiny_nanite/bunny.obj"));
    auto meshInputData = std::make_unique<MeshInputData>();
    ConvertData(primData->buffers[POSITION_ATTRIBUTE_NAME], meshInputData->Vertices.Positions);
    ConvertData(primData->buffers[NORMAL_ATTRIBUTE_NAME], meshInputData->Vertices.Normals);
    ConvertData(primData->buffers[TEXCOORD_ATTRIBUTE_NAME], meshInputData->Vertices.UVs);
    ConvertData(primData->indexs, meshInputData->TriangleIndices);
    meshInputData->TriangleCounts.push_back(meshInputData->TriangleIndices.size() / 3);
    meshInputData->MaterialIndices = {0};
    std::vector<Cluster> clusters;
    clusterTriangles1(clusters, *meshInputData, 0, meshInputData->TriangleIndices.size() / 3);

    std::unordered_map<uint32_t, glm::vec3> idToColors;
    for (int i = 0; i < meshInputData->TriangleIndices.size(); i++) {
        auto index = meshInputData->TriangleIndices[i];
        auto part  = 0;
        if (idToColors.find(index) == idToColors.end()) {
            idToColors[index] = getClusterColor(part);
        }
    }

    std::vector<glm::vec3> colors(idToColors.size());
    for (auto& [id, color] : idToColors) {
        colors[id] = color;
    }
    // for (int i = 0; i < meshInputData->TriangleIndices.size() / 3; i++) {
    //     for (int k = 0; k < 3; k++) {
    //         idToColors.push_back(pseudocolor(partId[i]));
    //     }
    // }
    auto primitve                       = std::make_unique<Primitive>(0, 0, meshInputData->Vertices.Positions.size(), meshInputData->TriangleIndices.size(), 0);
    primData->vertexAttributes["color"] = {VK_FORMAT_R32G32B32_SFLOAT, sizeof(glm::vec3)};
    primData->buffers["color"]          = std::vector<uint8_t>((uint8_t*)colors.data(), (uint8_t*)colors.data() + idToColors.size() * sizeof(glm::vec3));

    return SceneLoaderInterface::loadPrimitiveFromPritiveData(g_context->getDevice(), primData.get());
}

float Cluster::simplify(uint32_t targetNumTris) {
    return 0.f;
}
GraphPartitioner::GraphPartitioner(uint32_t elementsNum, uint32_t targetPart) {
    this->targetPart = targetPart;
    numElements      = elementsNum;
    indexes.resize(numElements);
    for (uint32_t i = 0; i < numElements; i++) {
        indexes[i] = i;
    }
    partitionIDs.resize(numElements);
}
void GraphPartitioner::partition(FGraphData& graph) {
    idx_t NumConstraints = 1;
    int   NumParts       = targetPart;
    partitionIDs.resize(numElements);
    std::fill(partitionIDs.begin(), partitionIDs.end(), 0);

    // METIS要求的参数
    idx_t nvtxs  = numElements;// 顶点数量
    idx_t ncon   = 1;          // 约束数量（通常为1）
    idx_t nparts = NumParts;   // 分区数量

    // METIS选项
    idx_t options[METIS_NOPTIONS];
    METIS_SetDefaultOptions(options);
    options[METIS_OPTION_NUMBERING] = 0;
    options[METIS_OPTION_OBJTYPE] = METIS_OBJTYPE_CUT;
    options[METIS_OPTION_SEED] = 14;
    // 权重数组
    std::vector<idx_t> vwgt(nvtxs, 1);// 顶点权重，默认为1
    std::vector<idx_t> adjwgt;        // 边权重

    // 转换边权重为idx_t类型
    if (!graph.AdjacencyCost.empty()) {
        adjwgt.resize(graph.AdjacencyCost.size());
        for (size_t i = 0; i < graph.AdjacencyCost.size(); i++) {
            adjwgt[i] = static_cast<idx_t>(graph.AdjacencyCost[i]);
        }
    }

    // 转换偏移数组为idx_t类型
    std::vector<idx_t> xadj(graph.AdjacencyOffset.size());
    for (size_t i = 0; i < graph.AdjacencyOffset.size(); i++) {
        xadj[i] = static_cast<idx_t>(graph.AdjacencyOffset[i]);
    }

    // 转换邻接数组为idx_t类型
    std::vector<idx_t> adjncy(graph.Adjacency.size());
    for (size_t i = 0; i < graph.Adjacency.size(); i++) {
        adjncy[i] = static_cast<idx_t>(graph.Adjacency[i]);
    }

    // 分区结果
    std::vector<idx_t> part(nvtxs);

    // 调试输出
    LOGI("METIS Input - Vertices: {}, Parts: {}, Edges: {}",
         nvtxs,
         nparts,
         graph.Adjacency.size());

    // 查数据有效性
    if (nvtxs < nparts) {
        LOGE("Number of vertices ({}) less than number of parts ({})", nvtxs, nparts);
        return;
    }

    // nvtxs = 0;
    int objval = 0;
    std::vector<float> tpwgts;
    tpwgts.resize(nparts, 1.0f / nparts);

    
    // 调用METIS
    int result = METIS_PartGraphRecursive(
        &nvtxs,                                  // 顶点数量
        &ncon,                                   // 约束数量
        xadj.data(),                             // 偏移数组
        adjncy.data(),                           // 邻接数组
        nullptr,                                 // 顶点权重 (nullptr表示所有权重为1)
        nullptr,            // 顶点大小 (nullptr表示所有大小为1)
        adjwgt.empty() ? nullptr : adjwgt.data(),  // 边权重
        &nparts,            // 分区数量
        nullptr,            // 目标分区大小
        nullptr,            // 分区权重
        options,            // 选项数组
        &objval,          // 输出：边切割数量
        part.data()         // 输出：分区结果
    );

    partitionIDs = part;
}
