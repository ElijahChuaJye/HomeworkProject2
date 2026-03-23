#pragma once

#include "geometry.h"
#include <vector>
#include <set>
#include <cmath>

// Represents a potential collapse of the segment between vertex B and C into a new point E.
struct CollapseCandidate {
    double displacement;
    
    // Pointers to the 4 vertices involved to easily update the linked list later
    Vertex* a;
    Vertex* b;
    Vertex* c;
    Vertex* d;
    
    // The calculated coordinates for the new area-preserving point E
    double e_x;
    double e_y;

    // Custom comparator for std::set so it acts as a priority queue.
    // It sorts by smallest displacement first. 
    bool operator<(const CollapseCandidate& other) const {
        // Strict weak ordering: If displacements are identical, use IDs as tie-breakers 
        // to prevent the set from overwriting unique, identical-scoring candidates.
        if (std::abs(displacement - other.displacement) > 1e-9) {
            return displacement < other.displacement;
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
bool is_collapse_valid(const std::vector<Ring>& polygon, const CollapseCandidate& cand);

// ---------------------------------------------------------
// Main Algorithm Loop
// ---------------------------------------------------------

double simplify_polygon(std::vector<Ring>& polygon, int target_vertices);