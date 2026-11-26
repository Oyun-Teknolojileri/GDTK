/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include "Bvh.h"

#include <algorithm>
#include <numeric>

namespace ToolKit
{
  namespace LightProbeBake
  {
    namespace
    {
      /** Triangles a leaf may hold before splitting stops being worth it. */
      constexpr uint MaxLeafSize = 8;

      /** Bins the binned SAH sweep uses along the chosen axis. */
      constexpr int BinCount = 12;

      /** Hard recursion cap. A pathological scene degrades to a deeper leaf, not a stack overflow. */
      constexpr int MaxDepth = 48;

      constexpr int TraversalStackSize = 64;

      inline float SurfaceArea(const Vec3& bmin, const Vec3& bmax)
      {
        const Vec3 d = bmax - bmin;
        return 2.0f * (d.x * d.y + d.y * d.z + d.z * d.x);
      }

      inline void Grow(Vec3& bmin, Vec3& bmax, const Vec3& p)
      {
        bmin = glm::min(bmin, p);
        bmax = glm::max(bmax, p);
      }

      inline Vec3 SafeInverse(const Vec3& d)
      {
        const auto inverse = [](float v) -> float
        {
          if (std::abs(v) > 1e-8f)
          {
            return 1.0f / v;
          }
          return v < 0.0f ? -1e8f : 1e8f;
        };

        return Vec3(inverse(d.x), inverse(d.y), inverse(d.z));
      }

      struct SlabResult
      {
        bool hit   = false;
        float near = 0.0f;
        float far  = 0.0f;
      };

      inline SlabResult Slab(const Vec3& bmin, const Vec3& bmax, const Vec3& origin, const Vec3& invDir, float tMax)
      {
        const Vec3 t0     = (bmin - origin) * invDir;
        const Vec3 t1     = (bmax - origin) * invDir;
        const Vec3 tNear3 = glm::min(t0, t1);
        const Vec3 tFar3  = glm::max(t0, t1);

        SlabResult result;
        result.near = glm::max(glm::max(tNear3.x, tNear3.y), glm::max(tNear3.z, 0.0f));
        result.far  = glm::min(glm::min(tFar3.x, tFar3.y), glm::min(tFar3.z, tMax));
        result.hit  = result.far >= result.near;
        return result;
      }
    } // namespace

    bool Bvh::IntersectTriangle(uint triangle,
                                const Vec3& origin,
                                const Vec3& direction,
                                float& t,
                                float& u,
                                float& v) const
    {
      const Triangle& tri = m_triangles[triangle];

      const Vec3 edge1 = tri.p1 - tri.p0;
      const Vec3 edge2 = tri.p2 - tri.p0;
      const Vec3 pvec  = glm::cross(direction, edge2);

      // Two sided: the inside of a room is shaded by the same surface as its outside.
      const float det = glm::dot(edge1, pvec);
      if (std::abs(det) < 1e-9f)
      {
        return false;
      }

      const float invDet = 1.0f / det;
      const Vec3 tvec    = origin - tri.p0;

      u = glm::dot(tvec, pvec) * invDet;
      if (u < 0.0f || u > 1.0f)
      {
        return false;
      }

      const Vec3 qvec = glm::cross(tvec, edge1);
      v               = glm::dot(direction, qvec) * invDet;
      if (v < 0.0f || u + v > 1.0f)
      {
        return false;
      }

      t = glm::dot(edge2, qvec) * invDet;
      return t > 1e-5f;
    }

    void Bvh::Build(std::vector<Triangle>&& triangles)
    {
      m_triangles = std::move(triangles);
      m_indices.clear();
      m_centroids.clear();
      m_nodes.clear();

      const uint count = (uint) m_triangles.size();
      if (count == 0)
      {
        m_boundsMin = Vec3(0.0f);
        m_boundsMax = Vec3(0.0f);
        return;
      }

      m_centroids.resize(count);
      for (uint i = 0; i < count; ++i)
      {
        m_centroids[i] = m_triangles[i].Centroid();
      }

      m_indices.resize(count);
      std::iota(m_indices.begin(), m_indices.end(), 0u);

      m_nodes.reserve(count * 2);
      BuildNode(0, count, 0);

      m_boundsMin = m_nodes[0].bmin;
      m_boundsMax = m_nodes[0].bmax;
    }

    uint Bvh::BuildNode(uint begin, uint end, int depth)
    {
      const uint nodeIndex = (uint) m_nodes.size();
      m_nodes.push_back(Node {});

      Vec3 bmin(FLT_MAX);
      Vec3 bmax(-FLT_MAX);
      Vec3 cmin(FLT_MAX);
      Vec3 cmax(-FLT_MAX);

      for (uint i = begin; i < end; ++i)
      {
        const uint index    = m_indices[i];
        const Triangle& tri = m_triangles[index];

        Grow(bmin, bmax, tri.p0);
        Grow(bmin, bmax, tri.p1);
        Grow(bmin, bmax, tri.p2);
        Grow(cmin, cmax, m_centroids[index]);
      }

      m_nodes[nodeIndex].bmin = bmin;
      m_nodes[nodeIndex].bmax = bmax;

      const uint count  = end - begin;
      const Vec3 extent = cmax - cmin;

      auto makeLeaf = [this, nodeIndex, begin, count]() -> uint
      {
        m_nodes[nodeIndex].first = begin;
        m_nodes[nodeIndex].count = count;
        return nodeIndex;
      };

      if (count <= MaxLeafSize || depth >= MaxDepth)
      {
        return makeLeaf();
      }

      int axis = 0;
      if (extent.y > extent.x)
      {
        axis = 1;
      }
      if (extent.z > extent[axis])
      {
        axis = 2;
      }

      if (extent[axis] < 1e-6f)
      {
        return makeLeaf();
      }

      // Binned SAH along the axis with the widest centroid spread.
      struct Bin
      {
        Vec3 bmin  = Vec3(FLT_MAX);
        Vec3 bmax  = Vec3(-FLT_MAX);
        uint count = 0;
      };

      Bin bins[BinCount];
      const float scale   = (float) BinCount / extent[axis];
      const float minAxis = cmin[axis];

      auto binOf = [&](uint index) -> int
      {
        const int bin = (int) ((m_centroids[index][axis] - minAxis) * scale);
        return glm::clamp(bin, 0, BinCount - 1);
      };

      for (uint i = begin; i < end; ++i)
      {
        const uint index    = m_indices[i];
        const Triangle& tri = m_triangles[index];

        Bin& bin = bins[binOf(index)];
        Grow(bin.bmin, bin.bmax, tri.p0);
        Grow(bin.bmin, bin.bmax, tri.p1);
        Grow(bin.bmin, bin.bmax, tri.p2);
        bin.count++;
      }

      float leftArea[BinCount]  = {};
      float rightArea[BinCount] = {};
      uint leftCount[BinCount]  = {};
      uint rightCount[BinCount] = {};

      {
        Vec3 runningMin(FLT_MAX);
        Vec3 runningMax(-FLT_MAX);
        uint runningCount = 0;
        for (int i = 0; i < BinCount; ++i)
        {
          if (bins[i].count > 0)
          {
            Grow(runningMin, runningMax, bins[i].bmin);
            Grow(runningMin, runningMax, bins[i].bmax);
          }
          runningCount += bins[i].count;
          leftCount[i]  = runningCount;
          leftArea[i]   = runningCount > 0 ? SurfaceArea(runningMin, runningMax) : 0.0f;
        }
      }

      {
        Vec3 runningMin(FLT_MAX);
        Vec3 runningMax(-FLT_MAX);
        uint runningCount = 0;
        for (int i = BinCount - 1; i >= 0; --i)
        {
          if (bins[i].count > 0)
          {
            Grow(runningMin, runningMax, bins[i].bmin);
            Grow(runningMin, runningMax, bins[i].bmax);
          }
          runningCount += bins[i].count;
          rightCount[i] = runningCount;
          rightArea[i]  = runningCount > 0 ? SurfaceArea(runningMin, runningMax) : 0.0f;
        }
      }

      float bestCost = FLT_MAX;
      int bestSplit  = -1;
      for (int i = 0; i < BinCount - 1; ++i)
      {
        if (leftCount[i] == 0 || rightCount[i + 1] == 0)
        {
          continue;
        }

        const float cost = leftArea[i] * (float) leftCount[i] + rightArea[i + 1] * (float) rightCount[i + 1];
        if (cost < bestCost)
        {
          bestCost  = cost;
          bestSplit = i;
        }
      }

      uint mid = begin;
      if (bestSplit >= 0)
      {
        const auto splitIt = std::partition(m_indices.begin() + begin,
                                            m_indices.begin() + end,
                                            [&](uint index) { return binOf(index) <= bestSplit; });

        mid = (uint) (splitIt - m_indices.begin());
      }
      else
      {
        // All centroids land in one bin. Median split so the build terminates.
        mid = begin + count / 2;
        std::nth_element(m_indices.begin() + begin,
                         m_indices.begin() + mid,
                         m_indices.begin() + end,
                         [&](uint a, uint b) { return m_centroids[a][axis] < m_centroids[b][axis]; });
      }

      if (mid == begin || mid == end)
      {
        return makeLeaf();
      }

      m_nodes[nodeIndex].count = 0;
      BuildNode(begin, mid, depth + 1); // Left child is always nodeIndex + 1.
      const uint right         = BuildNode(mid, end, depth + 1);
      m_nodes[nodeIndex].first = right;

      return nodeIndex;
    }

    bool Bvh::Intersect(const Vec3& origin, const Vec3& direction, float tMax, RayHit& hit) const
    {
      if (m_nodes.empty())
      {
        return false;
      }

      const Vec3 invDir = SafeInverse(direction);

      uint stack[TraversalStackSize];
      int stackSize = 0;
      stack[stackSize++] = 0;

      bool found = false;
      float best = tMax;

      while (stackSize > 0)
      {
        const uint nodeIndex = stack[--stackSize];
        const Node& node     = m_nodes[nodeIndex];

        const SlabResult slab = Slab(node.bmin, node.bmax, origin, invDir, best);
        if (!slab.hit)
        {
          continue;
        }

        if (node.count > 0)
        {
          for (uint i = node.first; i < node.first + node.count; ++i)
          {
            float t             = 0.0f;
            float u             = 0.0f;
            float v             = 0.0f;
            const uint triangle = m_indices[i];
            if (IntersectTriangle(triangle, origin, direction, t, u, v) && t < best)
            {
              best         = t;
              hit.t        = t;
              hit.triangle = triangle;
              hit.u        = u;
              hit.v        = v;
              found        = true;
            }
          }
          continue;
        }

        if (stackSize + 2 <= TraversalStackSize)
        {
          stack[stackSize++] = nodeIndex + 1;
          stack[stackSize++] = node.first;
        }
      }

      return found;
    }

    bool Bvh::Occluded(const Vec3& origin,
                       const Vec3& direction,
                       float tMax,
                       const std::vector<ubyte>* passing) const
    {
      if (m_nodes.empty())
      {
        return false;
      }

      const Vec3 invDir = SafeInverse(direction);

      uint stack[TraversalStackSize];
      int stackSize = 0;
      stack[stackSize++] = 0;

      while (stackSize > 0)
      {
        const uint nodeIndex = stack[--stackSize];
        const Node& node     = m_nodes[nodeIndex];

        const SlabResult slab = Slab(node.bmin, node.bmax, origin, invDir, tMax);
        if (!slab.hit)
        {
          continue;
        }

        if (node.count > 0)
        {
          for (uint i = node.first; i < node.first + node.count; ++i)
          {
            float t = 0.0f;
            float u = 0.0f;
            float v = 0.0f;
            if (IntersectTriangle(m_indices[i], origin, direction, t, u, v) && t < tMax)
            {
              if (passing != nullptr)
              {
                const uint material = m_triangles[m_indices[i]].material;
                if (material < passing->size() && (*passing)[material] != 0)
                {
                  // Light passes through this one, so it is not an occluder: look past it.
                  continue;
                }
              }

              return true;
            }
          }
          continue;
        }

        if (stackSize + 2 <= TraversalStackSize)
        {
          stack[stackSize++] = nodeIndex + 1;
          stack[stackSize++] = node.first;
        }
      }

      return false;
    }

  } // namespace LightProbeBake
} // namespace ToolKit
