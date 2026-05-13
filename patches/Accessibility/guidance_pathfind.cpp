#include "guidance_pathfind.h"

#include <windows.h>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

#include "engine_area.h"                // WallEdge, SegmentCrossesWalkmesh
#include "engine_navgraph.h"            // shared snapshot helpers
#include "log.h"
#include "spatial_change_detector.h"    // GetCachedWalls

namespace acc::guidance {

namespace {

// Re-use the navgraph caps so the A* state buffers below have stable
// sizes that match what SnapshotNavGraph can hand us.
constexpr int kMaxNodes = acc::engine::navgraph::kMaxNodes;
constexpr int kMaxEdges = acc::engine::navgraph::kMaxEdges;

using acc::engine::navgraph::PathPointSnapshot;

// Euclidean distance (2D in XY plane; KOTOR areas are layout-flat enough
// that Z doesn't materially affect path costs and the walkmesh edges in
// engine_area also work in XY). Used both as edge weight and as A* heuristic.
float DistXY(const Vector& a, const Vector& b) {
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

float DistXYSq(const Vector& a, const Vector& b) {
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    return dx * dx + dy * dy;
}

// Find the index of the path_point nearest to `target` in XY. Linear
// scan — N ≤ kMaxNodes, trivial cost per call.
int FindNearestNode(const std::vector<PathPointSnapshot>& nodes,
                    const Vector& target) {
    int n = static_cast<int>(nodes.size());
    if (n <= 0) return -1;
    int best = 0;
    float bestSq = DistXYSq(nodes[0].pos, target);
    for (int i = 1; i < n; ++i) {
        float d = DistXYSq(nodes[i].pos, target);
        if (d < bestSq) {
            bestSq = d;
            best   = i;
        }
    }
    return best;
}

// Diagnostic: find the closest (smallest-t along a→b) walkmesh edge that
// blocks the segment a→b. Mirrors SegmentCrossesWalkmesh's geometry but
// returns the offending edge instead of just the hit point. Returns -1
// when no edge blocks (segment is clear).
//
// Used only for smoothing-pass logging — we want to know WHICH edge
// rejected a candidate skip, so we can tell legitimate corridor walls
// apart from spurious room-mesh seams. Linear cost (same as the
// underlying test); fired at most O(N²) times per ComputePath where N
// is the raw waypoint count (typically <10), so total cost negligible.
int FindBlockingEdge(const acc::engine::WallEdge* walls, int wallCount,
                     const Vector& a, const Vector& b, float& outT) {
    if (!walls || wallCount <= 0) return -1;
    float abx = b.x - a.x;
    float aby = b.y - a.y;
    if (abx * abx + aby * aby < 1e-10f) return -1;

    int   bestIdx = -1;
    float bestT   = 1e30f;
    for (int i = 0; i < wallCount; ++i) {
        const acc::engine::WallEdge& w = walls[i];
        float cdx = w.b.x - w.a.x;
        float cdy = w.b.y - w.a.y;
        float denom = abx * cdy - aby * cdx;
        if (denom > -1e-8f && denom < 1e-8f) continue;
        float dx = w.a.x - a.x;
        float dy = w.a.y - a.y;
        float t  = (dx * cdy - dy * cdx) / denom;
        float u  = (dx * aby - dy * abx) / denom;
        if (t < 0.0f || t > 1.0f) continue;
        if (u < 0.0f || u > 1.0f) continue;
        if (t < bestT) {
            bestT   = t;
            bestIdx = i;
        }
    }
    outT = bestT;
    return bestIdx;
}

}  // namespace

bool ComputePath(void* area,
                 const Vector& start,
                 const Vector& goal,
                 std::vector<Vector>& outWaypoints) {
    outWaypoints.clear();
    if (!area) {
        acclog::Write("Pathfind", "ComputePath: area=null");
        return false;
    }

    acc::engine::navgraph::NavGraphSnapshot graph;
    if (!acc::engine::navgraph::SnapshotNavGraph(area, graph)) {
        acclog::Write("Pathfind", "ComputePath: nav graph empty / unreadable");
        return false;
    }

    const int nodeCount  = static_cast<int>(graph.nodes.size());
    const int connsCount = static_cast<int>(graph.conns.size());
    if (nodeCount <= 0) {
        acclog::Write("Pathfind", "ComputePath: no nodes loaded");
        return false;
    }

    acclog::Write("Pathfind", "graph nodes=%d conns=%d "
                  "start=(%.2f,%.2f,%.2f) goal=(%.2f,%.2f,%.2f)",
                  nodeCount, connsCount,
                  start.x, start.y, start.z,
                  goal.x,  goal.y,  goal.z);

    // Degenerate: start within reach of goal (0.5m). Skip the graph search
    // entirely; emit a single-point path so consumers can speak "already
    // at destination" without a special-case.
    if (DistXY(start, goal) < 0.5f) {
        outWaypoints.push_back(goal);
        acclog::Write("Pathfind", "ComputePath: start == goal (single-point path)");
        return true;
    }

    int startNode = FindNearestNode(graph.nodes, start);
    int goalNode  = FindNearestNode(graph.nodes, goal);
    if (startNode < 0 || goalNode < 0) {
        acclog::Write("Pathfind", "ComputePath: nearest-node resolution failed");
        return false;
    }

    // Degenerate: start and goal map to same nav-graph node. Path is just
    // the goal anchor.
    if (startNode == goalNode) {
        outWaypoints.push_back(goal);
        acclog::Write("Pathfind", "ComputePath: start/goal share node %d "
                      "(single-point path)", startNode);
        return true;
    }

    // A* over the static graph. Linear-array open set: kMaxNodes is small
    // enough that scanning for min-f beats heap maintenance. Closed flag
    // is part of the per-node state.
    struct AStarNode {
        float g       = std::numeric_limits<float>::infinity();
        float f       = std::numeric_limits<float>::infinity();
        int   parent  = -1;
        bool  closed  = false;
        bool  open    = false;
    };
    static AStarNode s_state[kMaxNodes];
    for (int i = 0; i < nodeCount; ++i) s_state[i] = AStarNode{};

    s_state[startNode].g    = 0.0f;
    s_state[startNode].f    = DistXY(graph.nodes[startNode].pos,
                                     graph.nodes[goalNode].pos);
    s_state[startNode].open = true;

    int expanded = 0;
    while (true) {
        // Pick the open-set entry with lowest f.
        int   best   = -1;
        float bestF  = std::numeric_limits<float>::infinity();
        for (int i = 0; i < nodeCount; ++i) {
            if (s_state[i].open && s_state[i].f < bestF) {
                bestF = s_state[i].f;
                best  = i;
            }
        }
        if (best < 0) {
            acclog::Write("Pathfind", "ComputePath: open set exhausted "
                          "(no path) expanded=%d", expanded);
            return false;
        }
        if (best == goalNode) break;  // found

        s_state[best].open   = false;
        s_state[best].closed = true;
        ++expanded;

        int lo = 0, hi = 0;
        acc::engine::navgraph::NeighbourRange(graph, best, lo, hi);
        for (int e = lo; e < hi; ++e) {
            int nb = static_cast<int>(graph.conns[e]);
            if (nb < 0 || nb >= nodeCount) continue;
            if (s_state[nb].closed) continue;

            float tentativeG = s_state[best].g +
                               DistXY(graph.nodes[best].pos,
                                      graph.nodes[nb].pos);
            if (tentativeG < s_state[nb].g) {
                s_state[nb].parent = best;
                s_state[nb].g      = tentativeG;
                s_state[nb].f      = tentativeG +
                                     DistXY(graph.nodes[nb].pos,
                                            graph.nodes[goalNode].pos);
                s_state[nb].open   = true;
            }
        }

        // Defensive: in a malformed graph the search could loop forever.
        // Bound by 2 × nodeCount.
        if (expanded > 2 * nodeCount) {
            acclog::Write("Pathfind", "ComputePath: aborting after %d expansions "
                          "(probable malformed graph)", expanded);
            return false;
        }
    }

    // Reconstruct path goal → start by following parents, then reverse.
    int   chain[kMaxNodes];
    int   len = 0;
    int   cur = goalNode;
    while (cur >= 0 && len < kMaxNodes) {
        chain[len++] = cur;
        if (cur == startNode) break;
        cur = s_state[cur].parent;
    }
    if (len == 0 || chain[len - 1] != startNode) {
        acclog::Write("Pathfind", "ComputePath: parent chain broken "
                      "(len=%d last=%d expected start=%d)",
                      len, len ? chain[len - 1] : -1, startNode);
        return false;
    }

    outWaypoints.reserve(len + 1);
    for (int i = len - 1; i >= 0; --i) {
        outWaypoints.push_back(graph.nodes[chain[i]].pos);
    }
    // Append the original goal as the terminal anchor — the last
    // path_point is `goalNode` (closest node to goal), but the user
    // actually wants to end up AT goal, not at the nearest nav node.
    outWaypoints.push_back(goal);

    acclog::Write("Pathfind", "ComputePath: solved len=%d expanded=%d "
                  "startNode=%d goalNode=%d (raw waypoints incl. goal=%zu)",
                  len, expanded, startNode, goalNode, outWaypoints.size());

    // String-pull pass with `start` (player position) as the implicit
    // anchor. The raw A* output is `[nearestNodeToStart, ..., goal]`,
    // which means the first beacon target is the closest *graph* node —
    // which can sit behind / sideways of the player when the player is
    // mid-corridor between two nav-graph nodes. Walking the path triples
    // (anchor, B, C) and dropping B whenever anchor→C is walkmesh-clear
    // eliminates that backwards-first-hop case (and any other redundant
    // graph-node bounce along the route).
    //
    // Anchor is the player's current position, not outWaypoints[0]: that
    // is exactly the asymmetry that motivates the pass. We do NOT prepend
    // `start` to the output — the consumer already knows where the player
    // is; the path is "where to go from here", not "current position +
    // where to go".
    //
    // Wall cache pulled from spatial::change_detector (built once per
    // area-load by Pillar 1's BuildAreaWallCache); if it isn't ready
    // yet (rare — would mean smoothing requested before the
    // change-detector ticked once after area enter), skip smoothing
    // and return the raw path. Safe degradation: first beacon target
    // is the previous behaviour.
    const acc::engine::WallEdge* walls = nullptr;
    int wallCount = 0;
    bool haveWalls = acc::spatial::change_detector::GetCachedWalls(
                         walls, wallCount)
                     && walls != nullptr && wallCount > 0;
    if (haveWalls && outWaypoints.size() >= 2) {
        acclog::Write("Pathfind", "Smooth: begin anchor=(%.2f,%.2f) "
                      "wallCount=%d raw=%zu",
                      start.x, start.y, wallCount, outWaypoints.size());
        std::vector<Vector> smoothed;
        smoothed.reserve(outWaypoints.size());
        Vector anchor = start;
        size_t i = 0;
        const size_t n = outWaypoints.size();
        while (i < n) {
            // Extend anchor → outWaypoints[j] as far as LOS allows.
            // bestJ tracks the farthest still-clear index from anchor.
            size_t bestJ = i;
            for (size_t j = i; j < n; ++j) {
                Vector hit{};
                bool blocked = acc::engine::SegmentCrossesWalkmesh(
                    walls, wallCount, anchor, outWaypoints[j], hit);
                if (blocked) {
                    // Diagnostic: log the specific edge that rejected this
                    // skip. We want to spot spurious room-mesh seams (edges
                    // sitting at the middle of an open corridor) vs.
                    // legitimate walls.
                    float blockT = 0.0f;
                    int   eIdx   = FindBlockingEdge(walls, wallCount,
                                                    anchor,
                                                    outWaypoints[j],
                                                    blockT);
                    if (eIdx >= 0) {
                        const acc::engine::WallEdge& e = walls[eIdx];
                        acclog::Write("Pathfind",
                            "Smooth: BLOCK anchor=(%.2f,%.2f) -> wp[%zu]"
                            "=(%.2f,%.2f) at t=%.3f hit~(%.2f,%.2f) "
                            "edge#%d a=(%.2f,%.2f) b=(%.2f,%.2f) "
                            "room=%d mat=%d",
                            anchor.x, anchor.y, j,
                            outWaypoints[j].x, outWaypoints[j].y,
                            blockT, hit.x, hit.y,
                            eIdx, e.a.x, e.a.y, e.b.x, e.b.y,
                            e.room_id, e.material_id);
                    } else {
                        acclog::Write("Pathfind",
                            "Smooth: BLOCK anchor=(%.2f,%.2f) -> wp[%zu]"
                            "=(%.2f,%.2f) hit~(%.2f,%.2f) (no edge "
                            "resolved — mismatch with primary test?)",
                            anchor.x, anchor.y, j,
                            outWaypoints[j].x, outWaypoints[j].y,
                            hit.x, hit.y);
                    }
                    break;
                }
                acclog::Write("Pathfind",
                    "Smooth: CLEAR anchor=(%.2f,%.2f) -> wp[%zu]"
                    "=(%.2f,%.2f)",
                    anchor.x, anchor.y, j,
                    outWaypoints[j].x, outWaypoints[j].y);
                bestJ = j;
            }
            if (bestJ == i) {
                // Even outWaypoints[i] is blocked from anchor — keep it
                // and advance. This shouldn't normally happen because A*
                // already routed through walkable graph nodes; if it does,
                // our wall-cache geometry disagrees with the engine's nav
                // graph for that segment, and we'd rather keep the engine-
                // approved node than drop it on a possibly-wrong test.
                acclog::Write("Pathfind",
                    "Smooth: KEEP wp[%zu]=(%.2f,%.2f) (first-step blocked)",
                    i, outWaypoints[i].x, outWaypoints[i].y);
                smoothed.push_back(outWaypoints[i]);
                anchor = outWaypoints[i];
                ++i;
            } else {
                acclog::Write("Pathfind",
                    "Smooth: ADVANCE to wp[%zu]=(%.2f,%.2f) "
                    "(skipped %zu intermediate)",
                    bestJ, outWaypoints[bestJ].x, outWaypoints[bestJ].y,
                    bestJ - i);
                smoothed.push_back(outWaypoints[bestJ]);
                anchor = outWaypoints[bestJ];
                i = bestJ + 1;
            }
        }
        acclog::Write("Pathfind", "ComputePath: smoothed %zu -> %zu waypoints",
                      outWaypoints.size(), smoothed.size());
        outWaypoints = std::move(smoothed);
    } else if (!haveWalls) {
        acclog::Write("Pathfind", "ComputePath: wall cache unavailable "
                      "(walls=%p count=%d) — skipping smoothing",
                      walls, wallCount);
    }

    return true;
}

}  // namespace acc::guidance
