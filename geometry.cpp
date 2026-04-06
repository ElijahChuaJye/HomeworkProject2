#include "geometry.h"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <iostream>

std::vector<Ring> load_polygon_from_csv(const std::string& filepath) {
    std::vector<Ring> polygon;
    std::ifstream file(filepath);
    
    if (!file.is_open()) {
        throw std::runtime_error("Error: Could not open input file " + filepath);
    }

    std::string line;
    // Skip the header line: "ring_id,vertex_id,x,y"
    if (!std::getline(file, line)) {
        throw std::runtime_error("Error: File is empty or missing header.");
    }

    int current_ring_id = -1;
    Vertex* current_ring_head = nullptr;
    Vertex* current_ring_tail = nullptr;
    int current_vertex_count = 0;

    while (std::getline(file, line)) {
        // Skip empty lines to prevent parsing errors
        if (line.empty() || line.find_first_not_of(" \r\n\t") == std::string::npos) {
            continue; 
        }

        std::stringstream ss(line);
        std::string token;
        
        int ring_id = 0;
        int vertex_id = 0;
        double x = 0.0;
        double y = 0.0;

        try {
            // Parse the comma-separated values
            std::getline(ss, token, ','); ring_id = std::stoi(token);
            std::getline(ss, token, ','); vertex_id = std::stoi(token);
            std::getline(ss, token, ','); x = std::stod(token);
            std::getline(ss, token, ','); y = std::stod(token);
        } catch (const std::exception& e) {
            std::cerr << "Warning: Skipping malformed line: " << line << "\n";
            continue;
        }

        // Create the new vertex dynamically
        Vertex* new_vertex = new Vertex(ring_id, vertex_id, x, y);

        if (ring_id != current_ring_id) {
            // We've encountered a new ring. 
            // First, close the PREVIOUS ring into a circular list if it exists.
            if (current_ring_head != nullptr && current_ring_tail != nullptr) {
                current_ring_tail->next = current_ring_head;
                current_ring_head->prev = current_ring_tail;
                polygon.back().active_vertex_count = current_vertex_count;
            }

            // Initialize the new ring structure
            Ring new_ring;
            new_ring.ring_id = ring_id;
            new_ring.head = new_vertex;
            polygon.push_back(new_ring);

            current_ring_id = ring_id;
            current_ring_head = new_vertex;
            current_ring_tail = new_vertex;
            current_vertex_count = 1;
        } else {
            // Add the new vertex to the end of the current ring's line
            current_ring_tail->next = new_vertex;
            new_vertex->prev = current_ring_tail;
            current_ring_tail = new_vertex;
            current_vertex_count++;
        }
    }

    // Edge case: Don't forget to close the very last ring after the loop finishes!
    if (current_ring_head != nullptr && current_ring_tail != nullptr) {
        current_ring_tail->next = current_ring_head;
        current_ring_head->prev = current_ring_tail;
        polygon.back().active_vertex_count = current_vertex_count;
    }

    file.close();
    return polygon;
}