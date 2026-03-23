#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include "geometry.h"
#include "apsc.h"

// Helper function to calculate the total signed area of the entire polygon.
// It uses the standard Shoelace formula. The math naturally results in positive 
// area for counter-clockwise rings (exterior) and negative for clockwise rings (holes).
double calculate_total_area(const std::vector<Ring>& polygon) {
    double total_area = 0.0;
    
    for (const auto& ring : polygon) {
        if (ring.active_vertex_count < 3) continue;
        
        double ring_area = 0.0;
        Vertex* curr = ring.head;
        do {
            Vertex* next_v = curr->next;
            // Shoelace step: (x1 * y2) - (x2 * y1)
            ring_area += (curr->x * next_v->y) - (next_v->x * curr->y);
            curr = next_v;
        } while (curr != ring.head);
        
        total_area += ring_area / 2.0;
    }
    
    return total_area;
}

int main(int argc, char* argv[]) {
    // 1. Parse command-line arguments
    if (argc != 3) {
        std::cerr << "Usage: ./simplify <input_file.csv> <target_vertices>\n";
        return 1;
    }

    std::string input_file = argv[1];
    int target_vertices = std::stoi(argv[2]);

    // 2. Load the polygon from the CSV
    std::vector<Ring> polygon;
    try {
        polygon = load_polygon_from_csv(input_file);
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }

    // 3. Calculate the initial area before modification
    double input_area = calculate_total_area(polygon);

    // 4. Run the APSC algorithm
    std::cerr << "Simplifying polygon down to " << target_vertices << " vertices...\n";
    double total_displacement = simplify_polygon(polygon, target_vertices);

    // 5. Calculate the final area (should be practically identical to input_area)
    double output_area = calculate_total_area(polygon);

    // ---------------------------------------------------------
    // STRICT CONSOLE OUTPUT SECTION (Do not use std::cerr here)
    // ---------------------------------------------------------

    // Print the CSV header
    std::cout << "ring_id,vertex_id,x,y\n";

    // Print the remaining active vertices
    int out_ring_id = 0;
    for (const auto& ring : polygon) {
        if (ring.active_vertex_count < 3) continue; // Skip rings that were entirely collapsed

        int out_vertex_id = 0;
        Vertex* curr = ring.head;
        do {
            if (curr->is_active) {
                // Ensure contiguous IDs as required by the assignment
                std::cout << out_ring_id << "," 
                          << out_vertex_id << ","
                          << curr->x << "," 
                          << curr->y << "\n";
                out_vertex_id++;
            }
            curr = curr->next;
        } while (curr != ring.head);
        
        out_ring_id++;
    }

    // Print the summary statistics in scientific notation
    std::cout << std::scientific << std::setprecision(6);
    std::cout << "Total signed area in input: " << input_area << "\n";
    std::cout << "Total signed area in output: " << output_area << "\n";
    std::cout << "Total areal displacement: " << total_displacement << "\n";

    // 6. Cleanup memory to prevent leaks
    for (auto& ring : polygon) {
        ring.cleanup();
    }

    return 0;
}