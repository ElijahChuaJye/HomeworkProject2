#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <fstream>
#include "geometry.h"
#include "apsc.h"

// Include the nlohmann JSON library
#include "json.hpp" 
using json = nlohmann::json;

// ---------------------------------------------------------
// Helper Functions
// ---------------------------------------------------------

// Calculates the total signed area of the polygon using the Shoelace formula
double calculate_total_area(const std::vector<Ring>& polygon) {
    double total_area = 0.0;
    for (const auto& ring : polygon) {
        if (ring.active_vertex_count < 3) continue;
        
        double ring_area = 0.0;
        Vertex* curr = ring.head;
        do {
            Vertex* next_v = curr->next;
            ring_area += (curr->x * next_v->y) - (next_v->x * curr->y);
            curr = next_v;
        } while (curr != ring.head);
        
        total_area += ring_area / 2.0;
    }
    return total_area;
}

// Generates a standalone HTML file containing an interactive Vega-Lite visualization
void export_vega_lite_html(const std::vector<Ring>& polygon, const std::string& filename) {
    json data_values = json::array();

    // 1. Extract the active vertices into a JSON array
    for (const auto& ring : polygon) {
        if (ring.active_vertex_count < 3) continue;

        int order = 0;
        Vertex* curr = ring.head;
        Vertex* first_vertex = nullptr;

        do {
            if (curr->is_active) {
                if (!first_vertex) first_vertex = curr;
                
                data_values.push_back({
                    {"x", curr->x},
                    {"y", curr->y},
                    {"ring_id", std::to_string(ring.ring_id)}, // String for discrete color legend
                    {"order", order++}
                });
            }
            curr = curr->next;
        } while (curr != ring.head);

        // Append the first point again to visually close the line loop
        if (first_vertex) {
            data_values.push_back({
                {"x", first_vertex->x},
                {"y", first_vertex->y},
                {"ring_id", std::to_string(ring.ring_id)},
                {"order", order}
            });
        }
    }

    // 2. Define the static Vega-Lite template using a C++ Raw String Literal 
    // This prevents the compiler from confusing JSON braces with C++ initializer lists
    json vega_spec = json::parse(R"(
    {
        "$schema": "https://vega.github.io/schema/vega-lite/v5.json",
        "description": "Polygon Simplification Visualization",
        "width": 700,
        "height": 700,
        "mark": {
            "type": "line", 
            "point": true,
            "strokeWidth": 2
        },
        "encoding": {
            "x": {
                "field": "x", 
                "type": "quantitative", 
                "scale": {"zero": false},
                "title": "X Coordinate"
            },
            "y": {
                "field": "y", 
                "type": "quantitative", 
                "scale": {"zero": false},
                "title": "Y Coordinate"
            },
            "color": {
                "field": "ring_id", 
                "type": "nominal",
                "title": "Ring ID"
            },
            "order": {
                "field": "order", 
                "type": "quantitative"
            },
            "tooltip": [
                {"field": "x", "type": "quantitative"},
                {"field": "y", "type": "quantitative"},
                {"field": "ring_id", "type": "nominal"}
            ]
        }
    }
    )");

    // 3. Inject the dynamic C++ data into the parsed template
    vega_spec["data"] = {{"values", data_values}};

    // 4. Write the HTML template to a file
    std::ofstream file(filename);
    if (file.is_open()) {
        file << "<!DOCTYPE html>\n<html>\n<head>\n"
             << "  <script src=\"https://cdn.jsdelivr.net/npm/vega@5.25.0\"></script>\n"
             << "  <script src=\"https://cdn.jsdelivr.net/npm/vega-lite@5.15.0\"></script>\n"
             << "  <script src=\"https://cdn.jsdelivr.net/npm/vega-embed@6.22.2\"></script>\n"
             << "</head>\n<body>\n"
             << "  <div id=\"vis\"></div>\n"
             << "  <script>\n"
             << "    const spec = " << vega_spec.dump(2) << ";\n"
             << "    vegaEmbed('#vis', spec);\n"
             << "  </script>\n"
             << "</body>\n</html>\n";
        file.close();
        
        // Use std::cerr for logging so it doesn't break the strict CSV standard output
        std::cerr << "Visualization generated: " << filename << "\n";
    } else {
        std::cerr << "Failed to write visualization file.\n";
    }
}

// ---------------------------------------------------------
// Main Driver
// ---------------------------------------------------------

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: ./simplify <input_file.csv> <target_vertices>\n";
        return 1;
    }

    std::string input_file = argv[1];
    int target_vertices = std::stoi(argv[2]);

    std::vector<Ring> polygon;
    try {
        polygon = load_polygon_from_csv(input_file);
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }

    double input_area = calculate_total_area(polygon);

    std::cerr << "Simplifying polygon down to " << target_vertices << " vertices...\n";
    double total_displacement = simplify_polygon(polygon, target_vertices);

    double output_area = calculate_total_area(polygon);

    // Generate the interactive HTML visualization before destroying the data
    export_vega_lite_html(polygon, "visualization.html");

    // ---------------------------------------------------------
    // STRICT CONSOLE OUTPUT SECTION (Do not use std::cerr here)
    // ---------------------------------------------------------
    std::cout << "ring_id,vertex_id,x,y\n";

    int out_ring_id = 0;
    for (const auto& ring : polygon) {
        if (ring.active_vertex_count < 3) continue; 

        int out_vertex_id = 0;
        Vertex* curr = ring.head;
        do {
            if (curr->is_active) {
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

    std::cout << std::scientific << std::setprecision(6);
    std::cout << "Total signed area in input: " << input_area << "\n";
    std::cout << "Total signed area in output: " << output_area << "\n";
    std::cout << "Total areal displacement: " << total_displacement << "\n";

    // Prevent memory leaks
    for (auto& ring : polygon) {
        ring.cleanup();
    }

    return 0;
}