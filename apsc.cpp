#include "apsc.h"
#include <iostream>
#include <algorithm>
#include <limits>

// ---------------------------------------------------------
// Core Math Functions
// ---------------------------------------------------------

double calculate_signed_area(double x1, double y1, double x2, double y2, double x3, double y3) {
    return ((x1 * (y2 - y3)) + (x2 * (y3 - y1)) + (x3 * (y1 - y2))) / 2.0;
}

CollapseCandidate evaluate_collapse(Vertex* a, Vertex* b, Vertex* c, Vertex* d) {
    CollapseCandidate cand;
    cand.a = a;
    cand.b = b;
    cand.c = c;
    cand.d = d;

    double area_BCD = calculate_signed_area(b->x, b->y, c->x, c->y, d->x, d->y);
    double area_ABC = calculate_signed_area(a->x, a->y, b->x, b->y, c->x, c->y);
    double denominator = area_BCD - area_ABC;

    double t;
    // The Kronenfeld paper states: if AD is parallel to BC, Area(ABC) == Area(BCD).
    // It is mathematically impossible to place E on BC to preserve area.
    if (std::abs(denominator) < 1e-9) {
        if (std::abs(area_BCD) < 1e-9) {
            t = 0.5; // Edge case: B, C, and D are perfectly collinear. Any t works.
        }
        else {
            // INVALID COLLAPSE: No area-preserving point exists!
            // Mark with infinite penalty so it is never chosen.
            cand.displacement = std::numeric_limits<double>::infinity();
            cand.score = std::numeric_limits<double>::infinity();
            return cand;
        }
    }
    else {
        t = area_BCD / denominator;
    }

    // If E is placed absurdly far outside the B-C segment, reject it instantly.
    if (t < -5.0 || t > 6.0) {
        cand.displacement = std::numeric_limits<double>::infinity();
        cand.score = std::numeric_limits<double>::infinity();
        return cand;
    }

    cand.e_x = b->x + t * (c->x - b->x);
    cand.e_y = b->y + t * (c->y - b->y);

    double area_ABE = calculate_signed_area(a->x, a->y, b->x, b->y, cand.e_x, cand.e_y);
    double area_CDE = calculate_signed_area(c->x, c->y, d->x, d->y, cand.e_x, cand.e_y);
    cand.displacement = std::abs(area_ABE) + std::abs(area_CDE);

    // ---------------------------------------------------------
    // APPROACH 3: Look-Ahead Heuristic Enhancement
    // ---------------------------------------------------------
    double look_ahead_penalty = 0.0;
    if (a->prev != nullptr && d->next != nullptr) {
        double future_disp_left = std::abs(calculate_signed_area(a->prev->x, a->prev->y, a->x, a->y, cand.e_x, cand.e_y));
        double future_disp_right = std::abs(calculate_signed_area(cand.e_x, cand.e_y, d->x, d->y, d->next->x, d->next->y));
        look_ahead_penalty = (future_disp_left + future_disp_right) * 0.15;
    }

    cand.score = cand.displacement + look_ahead_penalty;
    return cand;
}

// ---------------------------------------------------------
// Spatial Index Implementation
// ---------------------------------------------------------

void SpatialGrid::build(const std::vector<Ring>& polygon) {
    grid.clear();
    if (polygon.empty()) return;

    // 1. Find the bounding box of the entire polygon
    min_x = min_y = 1e9;
    max_x = max_y = -1e9;
    int total_vertices = 0;

    for (const auto& ring : polygon) {
        if (ring.active_vertex_count < 3) continue;
        Vertex* curr = ring.head;
        do {
            if (curr->x < min_x) min_x = curr->x;
            if (curr->y < min_y) min_y = curr->y;
            if (curr->x > max_x) max_x = curr->x;
            if (curr->y > max_y) max_y = curr->y;
            total_vertices++;
            curr = curr->next;
        } while (curr != ring.head);
    }

    // 2. Determine a dynamic cell size (Aiming for roughly sqrt(N) grid cells)
    double width = max_x - min_x;
    double height = max_y - min_y;
    int grid_dim = std::max(10, (int)std::sqrt(total_vertices));
    cell_size = std::max(width, height) / grid_dim;
    if (cell_size < 1e-6) cell_size = 1.0; // Prevent divide by zero

    // 3. Insert all active segments
    for (const auto& ring : polygon) {
        if (ring.active_vertex_count < 3) continue;
        Vertex* curr = ring.head;
        do {
            insert_segment(curr);
            curr = curr->next;
        } while (curr != ring.head);
    }
}

void SpatialGrid::insert_segment(Vertex* v) {
    if (!v->is_active || !v->next->is_active) return;
    
    // Find the bounding box of this single line segment
    double s_min_x = std::min(v->x, v->next->x);
    double s_max_x = std::max(v->x, v->next->x);
    double s_min_y = std::min(v->y, v->next->y);
    double s_max_y = std::max(v->y, v->next->y);

    int start_cx = std::floor((s_min_x - min_x) / cell_size);
    int end_cx = std::floor((s_max_x - min_x) / cell_size);
    int start_cy = std::floor((s_min_y - min_y) / cell_size);
    int end_cy = std::floor((s_max_y - min_y) / cell_size);

    // Insert the segment into every cell its bounding box overlaps
    for (int x = start_cx; x <= end_cx; ++x) {
        for (int y = start_cy; y <= end_cy; ++y) {
            grid[get_cell_key(x, y)].push_back(v);
        }
    }
}

void SpatialGrid::remove_segment(Vertex* v) {
    (void) v;
    // Note: To keep things incredibly fast, we actually use "Lazy Deletion" for the grid too.
    // We don't actively search and erase vectors here. 
    // We just filter out inactive vertices when we retrieve them in get_candidates.
}

std::unordered_set<Vertex*> SpatialGrid::get_candidates(double box_min_x, double box_min_y, 
                                                        double box_max_x, double box_max_y) const {
    std::unordered_set<Vertex*> candidates;
    
    int start_cx = std::floor((box_min_x - min_x) / cell_size);
    int end_cx = std::floor((box_max_x - min_x) / cell_size);
    int start_cy = std::floor((box_min_y - min_y) / cell_size);
    int end_cy = std::floor((box_max_y - min_y) / cell_size);

    for (int x = start_cx; x <= end_cx; ++x) {
        for (int y = start_cy; y <= end_cy; ++y) {
            auto it = grid.find(get_cell_key(x, y));
            if (it != grid.end()) {
                for (Vertex* v : it->second) {
                    // Only return segments that are still active
                    if (v->is_active && v->next->is_active) {
                        candidates.insert(v);
                    }
                }
            }
        }
    }
    return candidates;
}

// ---------------------------------------------------------
// Topology & Intersection Helpers
// ---------------------------------------------------------

bool ccw(double ax, double ay, double bx, double by, double cx, double cy) {
    return (cy - ay) * (bx - ax) > (by - ay) * (cx - ax);
}

bool segments_intersect(double ax, double ay, double bx, double by, 
                        double cx, double cy, double dx, double dy) {
    return ccw(ax, ay, cx, cy, dx, dy) != ccw(bx, by, cx, cy, dx, dy) &&
           ccw(ax, ay, bx, by, cx, cy) != ccw(ax, ay, bx, by, dx, dy);
}

// Verifies that collapsing B and C into E won't cause the new segments to cross any existing lines [cite: 137]
bool is_collapse_valid(const SpatialGrid& grid, const CollapseCandidate& cand) {
    // 1. Create a bounding box around the two NEW segments (A->E and E->D)
    double min_x = std::min({cand.a->x, cand.e_x, cand.d->x});
    double max_x = std::max({cand.a->x, cand.e_x, cand.d->x});
    double min_y = std::min({cand.a->y, cand.e_y, cand.d->y});
    double max_y = std::max({cand.a->y, cand.e_y, cand.d->y});

    // 2. Ask the grid for only the segments that exist in that small area
    std::unordered_set<Vertex*> nearby_segments = grid.get_candidates(min_x, min_y, max_x, max_y);

    // 3. Check for intersections ONLY against those nearby segments
    for (Vertex* curr : nearby_segments) {
        Vertex* next_v = curr->next;
        
        // Skip the specific segments we are actively removing
        if (curr == cand.a || curr == cand.b || curr == cand.c) {
            continue;
        }
        
        // Check new segment A->E (Skip checking against segments sharing A)
        if (curr != cand.a->prev && next_v != cand.a) {
            if (segments_intersect(cand.a->x, cand.a->y, cand.e_x, cand.e_y, 
                                   curr->x, curr->y, next_v->x, next_v->y)) return false;
        }
        
        // Check new segment E->D (Skip checking against segments sharing D)
        if (curr != cand.d && next_v != cand.d->next) {
            if (segments_intersect(cand.e_x, cand.e_y, cand.d->x, cand.d->y, 
                                   curr->x, curr->y, next_v->x, next_v->y)) return false;
        }
    }
    return true;
}

// ---------------------------------------------------------
// Main Algorithm Loop
// ---------------------------------------------------------

double simplify_polygon(std::vector<Ring>& polygon, int target_vertices) {
    double total_displacement = 0.0;
    std::set<CollapseCandidate> pq;
    int total_active_vertices = 0;

    SpatialGrid grid;
    grid.build(polygon);

    // 1. Initial Evaluation
    for (auto& ring : polygon) {
        total_active_vertices += ring.active_vertex_count;
        if (ring.active_vertex_count < 4) continue;
        
        Vertex* curr = ring.head;
        do {
            pq.insert(evaluate_collapse(curr, curr->next, curr->next->next, curr->next->next->next));
            curr = curr->next;
        } while (curr != ring.head);
    }

    // 2. The Main Collapse Loop
    while (total_active_vertices > target_vertices && !pq.empty()) {
        auto it = pq.begin();
        CollapseCandidate best = *it;
        pq.erase(it);

        // Lazy Deletion 1: Sequence Broken Check
       if (!best.b->is_active || !best.c->is_active ||
            best.b->prev != best.a || best.c->next != best.d) {
            continue;
        }
        // Lazy Deletion 2: Stale Data Check 
        CollapseCandidate current_state = evaluate_collapse(best.a, best.b, best.c, best.d);

        // Catch the infinity (invalidated collapse) OR a mismatched score
        if (std::isinf(current_state.score) || std::abs(best.score - current_state.score) > 1e-9) {
            continue;
        }

        // 3. Verify topology (Make sure to pass current_state, not best!)
        if (!is_collapse_valid(grid, current_state)) {
            continue;
        }

        // 4. Apply the Collapse using the safely validated current_state
        current_state.b->x = current_state.e_x;
        current_state.b->y = current_state.e_y;
        current_state.c->is_active = false;

        current_state.b->next = current_state.d;
        current_state.d->prev = current_state.b;

        grid.insert_segment(current_state.a);
        grid.insert_segment(current_state.b);

        total_active_vertices--;

        // We only add the true geometric displacement, not the score penalty
        total_displacement += current_state.displacement;

        // Update ring metadata
        int r_id = best.b->ring_id;
        if (r_id >= 0 && r_id < (int)polygon.size()) {
            Ring& affected_ring = polygon[r_id];
            affected_ring.active_vertex_count--;
    
            // If the head of the ring was removed, point it to the kept vertex B
            if (affected_ring.head == best.c) {
                affected_ring.head = best.b; 
            }
        }

        // 5. Re-evaluate the affected neighborhood
        if (current_state.b->ring_id >= 0) {
            Vertex* e = current_state.b;
            Vertex* a = e->prev;
            Vertex* prev_a = a->prev;
            Vertex* d = e->next;
            Vertex* next_d = d->next;

            pq.insert(evaluate_collapse(prev_a->prev, prev_a, a, e));
            pq.insert(evaluate_collapse(prev_a, a, e, d));
            pq.insert(evaluate_collapse(a, e, d, next_d));
            pq.insert(evaluate_collapse(e, d, next_d, next_d->next));
        }
    }

    return total_displacement;
}