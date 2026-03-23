#pragma once

#include <string>
#include <vector>

// The core node of our simplified DCEL.
// Represents a single point on the polygon boundary.
struct Vertex {
    int ring_id;
    int id; // The original vertex_id from the CSV
    double x;
    double y;

    // Pointers to the adjacent vertices in the ring
    Vertex* prev;
    Vertex* next;

    // True if this vertex is still part of the simplified polygon.
    // False if it has been collapsed out by the APSC algorithm.
    bool is_active; 

    // Constructor for easy initialization
    Vertex(int r_id, int v_id, double x_coord, double y_coord)
        : ring_id(r_id), id(v_id), x(x_coord), y(y_coord), 
          prev(nullptr), next(nullptr), is_active(true) {}
};

// Represents a single closed boundary (the exterior ring or an interior hole).
struct Ring {
    int ring_id;
    Vertex* head;               // Pointer to any active vertex in the circular list
    int active_vertex_count;    // Keeps track of how many vertices remain

    Ring() : ring_id(-1), head(nullptr), active_vertex_count(0) {}

    // Helper method to safely deallocate the linked list and prevent memory leaks
    void cleanup() {
        if (!head) return;
        
        Vertex* current = head;
        do {
            Vertex* next_node = current->next;
            delete current;
            current = next_node;
        } while (current != head && current != nullptr);
        
        head = nullptr;
        active_vertex_count = 0;
    }
};

// Function declaration for loading the polygon data from the input CSV file.
// The implementation will go in geometry.cpp.
std::vector<Ring> load_polygon_from_csv(const std::string& filepath);