#include "pathfinding.hpp"
#include <vector>
#include "navmesh.hpp"
#include "log.hpp"
#include "timed_function.hpp"
#include <queue>

namespace
{
    static float heuristic(const linalg::vec3 &a, const linalg::vec3 &b)
    {
        return linalg::euclidean_distance_between(a,b);
    }

    using Corridor = std::vector<const nav_polygon_t*>;

    /// Builds a corridor of polygons from a start point to an end point using the A* pathfinding algorithm.
    ///
    /// This function searches through the navigation mesh polygon graph to find an optimal path,
    /// represented as a sequence of adjacent polygons that form a corridor from start to end.
    /// The A* algorithm uses a heuristic to guide the search, prioritizing polygons that are
    /// closer to the goal, which typically results in faster pathfinding than uninformed search.
    ///
    /// @param nav The navigation mesh containing polygons and vertices to search through.
    /// @param start The starting position in world space (only x and z coordinates are used).
    /// @param end The ending position in world space (only x and z coordinates are used).
    /// 
    /// @return A Corridor object containing pointers to the sequence of polygons forming
    ///         the path from start to end. Returns an empty corridor if no path exists
    ///         (e.g., if start or end points are outside the navigation mesh).
    ///
    /// @note The function locates which polygon contains the start and end points using
    ///       2D queries (x, z coordinates). The path is guaranteed to exist only if both
    ///       points are within the navigation mesh bounds.
    Corridor build_corridor_using_A_Star(const navmesh_t &nav, const linalg::vec3 &start, const linalg::vec3 &end)
    {
        Corridor corridor;

        //@FIXME(SMIA): we need to add the navmesh to the bvh for fast polygon queries, 
        // then we can do A* on the polygon graph to find a corridor of polygons from start to end.
        // which polygon contains the start and end points?
        int start_poly_idx = nav.find_polygon(start.x, start.z);
        int end_poly_idx = nav.find_polygon(end.x, end.z);
        if (start_poly_idx == -1 || end_poly_idx == -1)
        {
            // no path if start or end is outside the navmesh
            log_warning("[pathfinding] start or end point is outside the navmesh.");
            return corridor;
        }

        // Helper to compute the centroid of a polygon (XZ plane)
        auto poly_centroid = [&](int poly_idx) -> linalg::vec3f
        {
            const nav_polygon_t &poly = nav.polygons[poly_idx];
            linalg::vec3f sum = {};
            for (int v : poly.verts)
                sum = sum + nav.vertices[v].pos;
            float n = (float)poly.verts.size();
            return linalg::vec3f{.x = sum.x / n, .y = sum.y / n, .z = sum.z / n};
        };

        // A* search on the polygon graph
        struct Node
        {
            int poly_idx;
            float g_cost; // cost from start to this node
            float f_cost; // g_cost + heuristic to end
            bool operator>(const Node &rhs) const { return f_cost > rhs.f_cost; }
        };
        std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open_list;
        std::vector<bool>  closed_list(nav.polygons.size(), false);
        std::vector<int>   came_from(nav.polygons.size(), -1);
        std::vector<float> best_g(nav.polygons.size(), std::numeric_limits<float>::infinity());

        best_g[start_poly_idx] = 0.f;
        open_list.push(Node{.poly_idx = start_poly_idx, .g_cost = 0.f, .f_cost = heuristic(poly_centroid(start_poly_idx), end)});

        // Main A* loop: priority queue sorted by f_cost = g_cost + heuristic.
        bool found = false;
        while (!open_list.empty())
        {
            Node current = open_list.top();
            open_list.pop();

            if (closed_list[current.poly_idx])
                continue; // already processed this polygon

            closed_list[current.poly_idx] = true;

            if (current.poly_idx == end_poly_idx)
            {
                found = true;
                break;
            }

            const nav_polygon_t &current_poly = nav.polygons[current.poly_idx];
            linalg::vec3f current_center = poly_centroid(current.poly_idx);

            for (int neighbor_idx : current_poly.neighbors)
            {
                if (neighbor_idx < 0 || closed_list[neighbor_idx])
                    continue;

                linalg::vec3f neighbor_center = poly_centroid(neighbor_idx);
                float tentative_g_cost = current.g_cost + linalg::euclidean_distance_between(current_center, neighbor_center);
                if (tentative_g_cost >= best_g[neighbor_idx])
                    continue; // already found a better path to this neighbor
                best_g[neighbor_idx] = tentative_g_cost;
                came_from[neighbor_idx] = current.poly_idx;
                float f_cost = tentative_g_cost + heuristic(neighbor_center, end);
                open_list.push(Node{.poly_idx = neighbor_idx, .g_cost = tentative_g_cost, .f_cost = f_cost});
            }
        }

        // Reconstruct the corridor by walking came_from back from end to start, then reverse.
        if (found)
        {
            for (int idx = end_poly_idx; idx != -1; idx = came_from[idx])
                corridor.push_back(&nav.polygons[idx]);
            std::reverse(corridor.begin(), corridor.end());
        }

        return corridor;
    }
}



std::vector<linalg::vec3> find_path(const navmesh_t &nav, const linalg::vec3 &start, const linalg::vec3 &end)
{
    timed_function();
    auto corridor = build_corridor_using_A_Star(nav, start, end);
    std::vector<linalg::vec3> path;
    if (corridor.empty())
    {
        log_terminal("[pathfinding] no path found from start to end.");
        return {};
    }

    // If start and end are in the same polygon, go directly.
    if (corridor.size() == 1)
    {
        path.push_back(start);
        path.push_back(end);
        return path;
    }

    // Build the path from portal midpoints.
    // The first waypoint is the start, then one midpoint per shared edge between
    // consecutive corridor polygons, then the end.
    path.push_back(start);

    for (int i = 0; i + 1 < (int)corridor.size(); ++i)
    {
        const nav_polygon_t *cur  = corridor[i];
        const nav_polygon_t *next = corridor[i + 1];

        // Find which edge of cur is shared with next.
        const int N = (int)cur->neighbors.size();
        for (int e = 0; e < N; ++e)
        {
            if (cur->neighbors[e] == (next - nav.polygons.data()))
            {
                // Edge e runs from verts[e] to verts[(e+1)%N].
                linalg::vec3f a = nav.vertices[cur->verts[e          ]].pos;
                linalg::vec3f b = nav.vertices[cur->verts[(e + 1) % N]].pos;
                path.push_back((a + b) * 0.5f);
                break;
            }
        }
    }

    path.push_back(end);
    return path;
}