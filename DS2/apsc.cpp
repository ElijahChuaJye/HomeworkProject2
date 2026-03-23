#include "apsc.h"
#include <iostream>
#include <algorithm>

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
    if (std::abs(denominator) < 1e-9) {
        t = 0.5; // Default to midpoint if lines are parallel
    } else {
        t = area_BCD / denominator;
    }

    cand.e_x = b->x + t * (c->x - b->x);
    cand.e_y = b->y + t * (c->y - b->y);

    double area_ABE = calculate_signed_area(a->x, a->y, b->x, b->y, cand.e_x, cand.e_y);
    double area_CDE = calculate_signed_area(c->x, c->y, d->x, d->y, cand.e_x, cand.e_y);
    
    cand.displacement = std::abs(area_ABE) + std::abs(area_CDE);
    return cand;
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
bool is_collapse_valid(const std::vector<Ring>& polygon, const CollapseCandidate& cand) {
    for (const auto& ring : polygon) {
        if (ring.active_vertex_count < 3) continue;

        Vertex* curr = ring.head;
        do {
            Vertex* next_v = curr->next;
            
            if (curr->is_active && next_v->is_active) {
                // Skip the specific segments we are actively removing
                if (curr == cand.a || curr == cand.b || curr == cand.c) {
                    curr = next_v;
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
            curr = next_v;
        } while (curr != ring.head);
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

        // Lazy Deletion 1: Check if the sequence was broken by a previous collapse
        if (!best.b->is_active || !best.c->is_active || 
            best.a->next != best.b || best.b->next != best.c || best.c->next != best.d) {
            continue; 
        }

        // Lazy Deletion 2: Stale Data Check (Verify math hasn't changed due to neighboring morphs)
        CollapseCandidate current_state = evaluate_collapse(best.a, best.b, best.c, best.d);
        if (std::abs(best.displacement - current_state.displacement) > 1e-9) {
            continue; 
        }

        // 3. Verify topology [cite: 137]
        if (!is_collapse_valid(polygon, best)) {
            continue; 
        }

        // 4. Apply the Collapse
        best.b->x = best.e_x;
        best.b->y = best.e_y;
        best.c->is_active = false;
        
        best.b->next = best.d;
        best.d->prev = best.b;

        total_active_vertices--;
        total_displacement += best.displacement;

        // Update ring metadata
        for (auto& ring : polygon) {
            if (ring.ring_id == best.b->ring_id) {
                ring.active_vertex_count--;
                if (ring.head == best.c) {
                    ring.head = best.b;
                }
                break;
            }
        }

        // 5. Re-evaluate the affected neighborhood (All 4 overlapping sequences)
        if (best.b->ring_id >= 0) { 
            Vertex* e = best.b;
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