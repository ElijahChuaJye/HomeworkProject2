#pragma once

#include "geometry.h"
#include <vector>
#include <set>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

// Represents a potential collapse of the segment between vertex B and C into a new point E.
// Represents a potential collapse of the segment between vertex B and C into a new point E.
struct CollapseCandidate {
    double displacement; // The true geometric displacement
    double score;        // NEW: displacement + Look-Ahead penalty for sorting
    
    Vertex* a;
    Vertex* b;
    Vertex* c;
    Vertex* d;
    
    double e_x;
    double e_y;

    // The Priority Queue now sorts based on the heuristically penalized SCORE
    bool operator<(const CollapseCandidate& other) const {
        if (std::abs(score - other.score) > 1e-9) {
            return score < other.score;
        }
        if (b->id != other.b->id) {
            return b->id < other.b->id;
        }
        return c->id < other.c->id;
    }
};

// ---------------------------------------------------------
// Core Math Functions
// ---------------------------------------------------------

double calculate_signed_area(double x1, double y1, double x2, double y2, double x3, double y3);
CollapseCandidate evaluate_collapse(Vertex* a, Vertex* b, Vertex* c, Vertex* d);

// ---------------------------------------------------------
// Topology & Intersection Helpers
// ---------------------------------------------------------

bool ccw(double ax, double ay, double bx, double by, double cx, double cy);
bool segments_intersect(double ax, double ay, double bx, double by, double cx, double cy, double dx, double dy);

// ---------------------------------------------------------
// Spatial Index (Uniform Grid)
// ---------------------------------------------------------

class SpatialGrid {
private:
    double min_x, min_y, max_x, max_y;
    double cell_size;
    
    // Maps a 64-bit cell ID to a list of vertices. 
    // (Each vertex represents the line segment starting at that vertex: v -> v->next)
    std::unordered_map<long long, std::vector<Vertex*>> grid;

    long long get_cell_key(int cx, int cy) const {
        return ((long long)cx << 32) | (cy & 0xFFFFFFFF);
    }

public:
    void build(const std::vector<Ring>& polygon);
    void insert_segment(Vertex* v);
    void remove_segment(Vertex* v);
    
    // Returns a unique set of segments that might intersect the given bounding box
    std::unordered_set<Vertex*> get_candidates(double box_min_x, double box_min_y, 
                                               double box_max_x, double box_max_y) const;
};
// Update this signature:
bool is_collapse_valid(const SpatialGrid& grid, const CollapseCandidate& cand);

// ---------------------------------------------------------
// Main Algorithm Loop
// ---------------------------------------------------------

double simplify_polygon(std::vector<Ring>& polygon, int target_vertices);