#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <fstream>
#include <chrono>
#include <sys/resource.h>
#include <cmath>
#include <algorithm>

#include "geometry.h"
#include "apsc.h"
#include "benchmark.h"

// Include the nlohmann JSON library
#include "json.hpp" 

using json = nlohmann::json;

// ---------------------------------------------------------
// Helper Functions (Math & OS)
// ---------------------------------------------------------
long get_peak_memory_kb() {
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    return usage.ru_maxrss;
}

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

// ---------------------------------------------------------
// 1. Presentation Visualizer (Stats + Graph on Screen)
// ---------------------------------------------------------
void export_presentation_html(const std::vector<Ring>& polygon, const std::string& filename, 
                              double in_area, double out_area, double disp, 
                              double time_sec, long mem_kb, int start_v, int end_v) {
    json data_values = json::array();
    for (const auto& ring : polygon) {
        if (ring.active_vertex_count < 3) continue;
        int order = 0;
        Vertex* curr = ring.head;
        Vertex* first_vertex = nullptr;
        do {
            if (curr->is_active) {
                if (!first_vertex) first_vertex = curr;
                data_values.push_back({{"x", curr->x}, {"y", curr->y}, {"ring_id", std::to_string(ring.ring_id)}, {"order", order++}});
            }
            curr = curr->next;
        } while (curr != ring.head);
        if (first_vertex) {
            data_values.push_back({{"x", first_vertex->x}, {"y", first_vertex->y}, {"ring_id", std::to_string(ring.ring_id)}, {"order", order}});
        }
    }

    std::ofstream file(filename);
    if (file.is_open()) {
        file << R"HTML(<!DOCTYPE html>
<html>
<head>
  <title>APSC Presentation View</title>
  <script src="https://cdn.jsdelivr.net/npm/vega@5.25.0"></script>
  <script src="https://cdn.jsdelivr.net/npm/vega-lite@5.15.0"></script>
  <script src="https://cdn.jsdelivr.net/npm/vega-embed@6.22.2"></script>
  <style>
    body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; margin: 0; display: flex; height: 100vh; background: #f4f7f6;}
    .sidebar { width: 350px; background: #2c3e50; color: white; padding: 30px; box-shadow: 2px 0 5px rgba(0,0,0,0.2); overflow-y: auto;}
    .main { flex-grow: 1; display: flex; justify-content: center; align-items: center; padding: 20px;}
    .stat-box { background: #34495e; padding: 15px; border-radius: 8px; margin-bottom: 15px; border-left: 4px solid #3498db;}
    h1 { margin-top: 0; border-bottom: 2px solid #3498db; padding-bottom: 10px; font-size: 24px;}
    .val { font-size: 1.2em; font-weight: bold; color: #2ecc71; display: block; margin-top: 5px;}
    .val.err { color: #e74c3c; }
  </style>
</head>
<body>
  <div class="sidebar">
    <h1>Algorithm Stats</h1>
    <div class="stat-box">Hardware Metrics
        <span class="val">Time: )HTML" << std::fixed << std::setprecision(4) << time_sec << R"HTML( s</span>
        <span class="val">Peak Mem: )HTML" << mem_kb << R"HTML( KB</span>
    </div>
    <div class="stat-box">Vertex Reduction
        <span class="val">Input: )HTML" << start_v << R"HTML( vertices</span>
        <span class="val">Output: )HTML" << end_v << R"HTML( vertices</span>
    </div>
    <div class="stat-box">Accuracy (Areal)
        <span class="val">Input Area: )HTML" << std::scientific << std::setprecision(4) << in_area << R"HTML(</span>
        <span class="val">Output Area: )HTML" << out_area << R"HTML(</span>
        <span class="val err">Displacement: )HTML" << disp << R"HTML(</span>
    </div>
  </div>
  <div class="main"><div id="vis"></div></div>
  <script>
    const spec = {
      "$schema": "https://vega.github.io/schema/vega-lite/v5.json",
      "width": 700, "height": 700,
      "mark": {"type": "line", "point": true, "strokeWidth": 2},
      "data": {"values": )HTML" << data_values.dump() << R"HTML(},
      "encoding": {
        "x": {"field": "x", "type": "quantitative", "scale": {"zero": false}},
        "y": {"field": "y", "type": "quantitative", "scale": {"zero": false}},
        "color": {"field": "ring_id", "type": "nominal"},
        "order": {"field": "order", "type": "quantitative"}
      }
    };
    vegaEmbed('#vis', spec);
  </script>
</body>
</html>)HTML";
        file.close();
        std::cerr << "Presentation View generated: " << filename << "\n";
    }
}

// ---------------------------------------------------------
// 2. Automated Graph Generator (For Rubric Requirements)
// ---------------------------------------------------------
void run_graph_generator() {
    struct TestCase { std::string file; std::string prop; int target; };
    
    // UPDATE THESE FILES to match your actual test_cases directory!
    std::vector<TestCase> tests = {
        {"test_cases/small_blob.csv", "Space/Time Scaling", 10},
        {"test_cases/input_blob_with_two_holes.csv", "Space/Time Scaling", 15},
        // To show displacement curves, use the SAME file but drop the target vertices
        {"test_cases/input_blob_with_two_holes.csv", "Displacement Curve", 30},
        {"test_cases/input_blob_with_two_holes.csv", "Displacement Curve", 20},
        {"test_cases/input_blob_with_two_holes.csv", "Displacement Curve", 10}
    };

    json data_vals = json::array();
    std::cerr << "Generating required graphs...\n";

    for (const auto& t : tests) {
        auto poly = load_polygon_from_csv(t.file);
        int n = 0; for (const auto& r : poly) n += r.active_vertex_count;
        
        auto start = std::chrono::high_resolution_clock::now();
        double disp = simplify_polygon(poly, t.target);
        auto end = std::chrono::high_resolution_clock::now();
        
        data_vals.push_back({
            {"file", t.file}, {"property", t.prop}, {"n", n}, {"target", t.target},
            {"time", std::chrono::duration<double>(end - start).count()},
            {"memory", get_peak_memory_kb()}, {"displacement", disp}
        });
        for (auto& r : poly) r.cleanup();
    }

    std::ofstream file("graphs.html");
    if (file.is_open()) {
        file << R"HTML(<!DOCTYPE html>
<html>
<head>
  <title>Experimental Evaluation Graphs</title>
  <script src="https://cdn.jsdelivr.net/npm/vega@5.25.0"></script>
  <script src="https://cdn.jsdelivr.net/npm/vega-lite@5.15.0"></script>
  <script src="https://cdn.jsdelivr.net/npm/vega-embed@6.22.2"></script>
  <style> body{font-family: sans-serif; margin:40px; background:#f9f9f9;} .chart{display:inline-block; width:45%; background:white; padding:15px; margin:10px; box-shadow:0 2px 5px rgba(0,0,0,0.1);}</style>
</head>
<body>
  <h1>Algorithm Complexity & Accuracy Graphs</h1>
  <div id="time_chart" class="chart"></div>
  <div id="mem_chart" class="chart"></div>
  <div id="disp_chart" class="chart" style="width: 93%;"></div>

  <script>
    const data = )HTML" << data_vals.dump() << R"HTML(;
    
    vegaEmbed('#time_chart', {
      "$schema": "https://vega.github.io/schema/vega-lite/v5.json", "title": "Time Complexity (Time vs Input Size)",
      "data": {"values": data.filter(d => d.property === "Space/Time Scaling")},
      "mark": {"type": "line", "point": true}, "encoding": {"x": {"field": "n", "type": "quantitative", "title": "Initial Vertices"}, "y": {"field": "time", "type": "quantitative", "title": "Seconds"}}
    });

    vegaEmbed('#mem_chart', {
      "$schema": "https://vega.github.io/schema/vega-lite/v5.json", "title": "Space Complexity (Peak Mem vs Input Size)",
      "data": {"values": data.filter(d => d.property === "Space/Time Scaling")},
      "mark": {"type": "line", "point": true}, "encoding": {"x": {"field": "n", "type": "quantitative", "title": "Initial Vertices"}, "y": {"field": "memory", "type": "quantitative", "title": "Peak Memory (KB)"}}
    });

    vegaEmbed('#disp_chart', {
      "$schema": "https://vega.github.io/schema/vega-lite/v5.json", "title": "Accuracy (Areal Displacement vs Target)",
      "data": {"values": data.filter(d => d.property === "Displacement Curve")},
      "mark": {"type": "line", "point": true}, "encoding": {"x": {"field": "target", "type": "quantitative", "sort": "descending", "title": "Target Vertices (Lower is more simplified)"}, "y": {"field": "displacement", "type": "quantitative", "title": "Areal Displacement Error"}, "color": {"field": "file", "type": "nominal"}}
    });
  </script>
</body>
</html>)HTML";
        file.close();
        std::cerr << "SUCCESS: Opened graphs.html in your browser to see the graphs!\n";
    }
}

// ---------------------------------------------------------
// Main Driver
// ---------------------------------------------------------
int main(int argc, char* argv[]) {

    if (argc == 2 && std::string(argv[1]) == "--benchmark") {
    // Rubric-Specific Test Suite
    std::vector<TestCase> suite = {
        // (a) & (b): Scaling Tests (High Vertex Count)
        {"test_cases/city_map_10k.csv", "High Vertex Count", "Tests O(n log n) priority queue and O(1) ring updates.", 5000},
        {"test_cases/country_map_50k.csv", "High Vertex Count", "Verifies scaling logic as data size increases 5x.", 25000},
        {"test_cases/continent_100k.csv", "High Vertex Count", "Required by rubric to show competitive time on 100k+ vertices.", 50000},

        // Targeted Property Tests
        {"test_cases/swiss_cheese.csv", "Large Number of Holes", "Tests spatial grid efficiency with many internal boundaries.", 500},
        {"test_cases/coastline_jagged.csv", "Narrow Gaps", "Tests if intersection checks prevent rings from touching in thin areas.", 1000},
        {"test_cases/grid_aligned.csv", "Near-Degeneracies", "Tests floating-point stability with collinear points and zero areas.", 200},

        // (c): Displacement vs. Target (Using one complex file at different levels)
        {"test_cases/continent_100k.csv", "Displacement Curve", "90% simplification", 90000},
        {"test_cases/continent_100k.csv", "Displacement Curve", "70% simplification", 70000},
        {"test_cases/continent_100k.csv", "Displacement Curve", "50% simplification", 50000},
        {"test_cases/continent_100k.csv", "Displacement Curve", "30% simplification", 30000},
        {"test_cases/continent_100k.csv", "Displacement Curve", "10% simplification", 10000}
    };

    BenchmarkSuite runner;
    runner.run_and_export_html(suite);
    return 0;
}


    if (argc != 3) {
        std::cerr << "Usage for Presentation: ./simplify <input_file.csv> <target_vertices>\n";
        std::cerr << "Usage for Graphs:       ./simplify --benchmark\n";
        return 1;
    }

    std::string input_file = argv[1];
    int target_vertices = std::stoi(argv[2]);

    std::vector<Ring> polygon;
    try { polygon = load_polygon_from_csv(input_file); }
    catch (const std::exception& e) { std::cerr << e.what() << "\n"; return 1; }

    int start_v = 0;
    for (const auto& ring : polygon) start_v += ring.active_vertex_count;
    double input_area = calculate_total_area(polygon);

    // Track Time and Memory for the Presentation screen
    auto start_time = std::chrono::high_resolution_clock::now();
    double total_displacement = simplify_polygon(polygon, target_vertices);
    auto end_time = std::chrono::high_resolution_clock::now();
    
    std::chrono::duration<double> diff = end_time - start_time;
    long mem_kb = get_peak_memory_kb();

    int end_v = 0;
    for (const auto& ring : polygon) end_v += ring.active_vertex_count;
    double output_area = calculate_total_area(polygon);

    // Generate the Presentation Dashboard
    export_presentation_html(polygon, "visualization.html", input_area, output_area, total_displacement, diff.count(), mem_kb, start_v, end_v);

    // Standard CSV Console Output
    std::cout << "ring_id,vertex_id,x,y\n";
    int out_ring_id = 0;
    for (const auto& ring : polygon) {
        if (ring.active_vertex_count < 3) continue;
        int out_vertex_id = 0;
        Vertex* curr = ring.head;
        do {
            if (curr->is_active) {
                std::cout << out_ring_id << "," << out_vertex_id++ << "," << curr->x << "," << curr->y << "\n";
            }
            curr = curr->next;
        } while (curr != ring.head);
        out_ring_id++;
    }

    std::cout << std::scientific << std::setprecision(6);
    std::cout << "Total signed area in input: " << input_area << "\n";
    std::cout << "Total signed area in output: " << output_area << "\n";
    std::cout << "Total area displacement: " << total_displacement << "\n";

    for (auto& ring : polygon) ring.cleanup();
    return 0;
}