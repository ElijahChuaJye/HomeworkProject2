#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <fstream>
#include <chrono>
#include <sys/resource.h>

#include "geometry.h"
#include "apsc.h"
#include "benchmark.h" 
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
// Presentation Visualizer (Red vs Blue Dashboard)
// ---------------------------------------------------------
void export_presentation_html(const json& orig_vals, const json& simp_vals, const std::string& filename, 
                              double in_area, double out_area, double disp, 
                              double algo_time_sec, long mem_kb, int start_v, int end_v,
                              double load_time_sec, long baseline_mem_kb) {
    
    json geom_data = json::array({
        {{"category", "Vertices"}, {"type", "Initial"}, {"value", start_v}},
        {{"category", "Vertices"}, {"type", "Final"}, {"value", end_v}}
    });

    // FIX: Blue (Final) now represents the FULL time (Load + Algo)
    double total_program_time = load_time_sec + algo_time_sec;

    json perf_data = json::array({
        {{"category", "Time (s)"}, {"type", "Initial"}, {"value", load_time_sec}},
        {{"category", "Time (s)"}, {"type", "Final"}, {"value", total_program_time}},
        {{"category", "Peak Mem (KB)"}, {"type", "Initial"}, {"value", baseline_mem_kb}},
        {{"category", "Peak Mem (KB)"}, {"type", "Final"}, {"value", mem_kb}}
    });

    json err_data = json::array({
        {{"category", "Areal Displacement"}, {"type", "Initial"}, {"value", 0.0}},
        {{"category", "Areal Displacement"}, {"type", "Final"}, {"value", disp}}
    });

    std::ofstream file(filename);
    if (file.is_open()) {
        file << R"HTML(<!DOCTYPE html>
<html>
<head>
  <title>APSC Presentation Dashboard</title>
  <script src="https://cdn.jsdelivr.net/npm/vega@5"></script>
  <script src="https://cdn.jsdelivr.net/npm/vega-lite@5"></script>
  <script src="https://cdn.jsdelivr.net/npm/vega-embed@6"></script>
  <style>
    body { font-family: 'Segoe UI', sans-serif; margin: 0; display: flex; height: 100vh; background: #f4f7f6;}
    .sidebar { width: 450px; background: #2c3e50; color: white; padding: 20px; overflow-y: auto;}
    .main { flex-grow: 1; display: flex; justify-content: center; align-items: center; padding: 20px;}
    h1 { margin-top: 0; border-bottom: 2px solid #e74c3c; padding-bottom: 10px; font-size: 24px;}
    .stat-box { background: #34495e; padding: 12px; border-radius: 8px; margin-bottom: 10px; border-left: 4px solid #3498db;}
    .val { font-size: 1.05em; font-weight: bold; display: block; margin-top: 5px;}
    .chart-container { background: white; padding: 10px; border-radius: 8px; margin-bottom: 15px;}
    .explainer { font-size: 0.85em; color: #bdc3c7; margin-bottom: 10px; }
  </style>
</head>
<body>
  <div class="sidebar">
    <h1>Algorithm Execution Stats</h1>
    <p class="explainer">Red = Initial State / Data Load Time<br>Blue = Final State / Total Run Time</p>
    
    <div class="stat-box">Area Details
        <span class="val" style="color:#f1c40f">Input Area: )HTML" << std::scientific << std::setprecision(6) << in_area << R"HTML(</span>
        <span class="val" style="color:#f1c40f">Output Area: )HTML" << out_area << R"HTML(</span>
        <span class="val" style="color:#e74c3c">Displacement: )HTML" << disp << R"HTML(</span>
    </div>

    <div class="chart-container" id="geom_chart"></div>
    <div class="chart-container" id="perf_chart"></div>
    <div class="chart-container" id="err_chart"></div>
  </div>
  
  <div class="main"><div id="vis"></div></div>
  
  <script>
    const origData = )HTML" << orig_vals.dump() << R"HTML(;
    const simpData = )HTML" << simp_vals.dump() << R"HTML(;
    const geomData = )HTML" << geom_data.dump() << R"HTML(;
    const perfData = )HTML" << perf_data.dump() << R"HTML(;
    const errData = )HTML" << err_data.dump() << R"HTML(;

    vegaEmbed('#vis', {
      "$schema": "https://vega.github.io/schema/vega-lite/v5.json",
      "width": 650, "height": 650, 
      "title": {"text": "Shape Overlay", "subtitle": "Red Glow = Initial Shape | Blue Dashed = Simplified Polygon"},
      "layer": [
        {
          "data": {"values": origData},
          "mark": {"type": "line", "point": {"size": 10, "color": "#e74c3c"}, "color": "#e74c3c", "strokeWidth": 7, "opacity": 1.0},
          "encoding": {
            "x": {"field": "x", "type": "quantitative", "scale": {"zero": false}},
            "y": {"field": "y", "type": "quantitative", "scale": {"zero": false}},
            "order": {"field": "order", "type": "quantitative"},
            "detail": {"field": "ring_id"}
          }
        },
        {
          "data": {"values": simpData},
          "mark": {"type": "line", "point": {"size": 20, "color": "#2980b9"}, "color": "#113f60", "strokeWidth": 2, "opacity": 1.0, "strokeDash": [6, 4]},
          "encoding": {
            "x": {"field": "x", "type": "quantitative", "scale": {"zero": false}},
            "y": {"field": "y", "type": "quantitative", "scale": {"zero": false}},
            "detail": {"field": "ring_id"},
            "order": {"field": "order", "type": "quantitative"}
          }
        }
      ]
    });

    vegaEmbed('#geom_chart', {
      "$schema": "https://vega.github.io/schema/vega-lite/v5.json",
      "data": {"values": geomData},
      "facet": {"field": "category", "type": "nominal", "header": {"title": null}},
      "spec": {
        "width": 100, "height": 70,
        "layer": [
          {"mark": {"type": "bar", "cornerRadiusEnd": 4}},
          {"mark": {"type": "text", "align": "center", "baseline": "bottom", "dy": -3, "fontSize": 11, "color": "black"}, "encoding": {"text": {"field": "value", "type": "quantitative", "format": ".3g"}}}
        ],
        "encoding": {
          "x": {"field": "type", "type": "nominal", "axis": {"title": "", "labels": false, "ticks": false}},
          "y": {"field": "value", "type": "quantitative", "axis": {"title": "Count"}},
          "color": {"field": "type", "type": "nominal", "scale": {"domain": ["Initial", "Final"], "range": ["#e74c3c", "#3498db"]}, "legend": {"title": "State", "orient": "bottom"}}
        }
      },
      "resolve": {"scale": {"y": "independent"}}
    });

    // FIX: Added text layer to print exact values on top of the bars
    vegaEmbed('#perf_chart', {
      "$schema": "https://vega.github.io/schema/vega-lite/v5.json",
      "description": "Comparison of Loading vs Processing",
      "data": {"values": perfData},
      "facet": {"field": "category", "type": "nominal", "header": {"title": null}},
      "spec": {
        "width": 100, "height": 80,
        "layer": [
          {"mark": {"type": "bar", "cornerRadiusEnd": 4}},
          {"mark": {"type": "text", "align": "center", "baseline": "bottom", "dy": -2, "fontSize": 10, "color": "black"}, 
           "encoding": {"text": {"field": "value", "type": "quantitative", "format": ".3g"}}}
        ],
        "encoding": {
          "x": {"field": "type", "type": "nominal", "axis": {"title": null, "labelAngle": 0}},
          "y": {"field": "value", "type": "quantitative", "axis": {"title": null}},
          "color": {
            "field": "type", "type": "nominal", 
            "scale": {"domain": ["Initial", "Final"], "range": ["#e74c3c", "#3498db"]},
            "legend": null
          }
        }
      },
      "resolve": {"scale": {"y": "independent"}}
    });

    vegaEmbed('#err_chart', {
      "$schema": "https://vega.github.io/schema/vega-lite/v5.json",
      "width": 300, "height": 50, "title": "Accuracy Penalty",
      "data": {"values": errData},
      "layer": [
        {"mark": {"type": "bar", "cornerRadiusEnd": 4}},
        {"mark": {"type": "text", "align": "left", "baseline": "middle", "dx": 5, "fontSize": 11, "color": "black"}, "encoding": {"text": {"field": "value", "type": "quantitative", "format": ".3e"}}}
      ],
      "encoding": {
        "x": {"field": "value", "type": "quantitative", "axis": {"title": "Total Areal Displacement"}},
        "y": {"field": "type", "type": "nominal", "axis": {"title": ""}},
        "color": {"field": "type", "type": "nominal", "scale": {"domain": ["Initial", "Final"], "range": ["#e74c3c", "#3498db"]}, "legend": null}
      }
    });
  </script>
</body>
</html>)HTML";
        file.close();
    }
}

// ---------------------------------------------------------
// Main Driver
// ---------------------------------------------------------
int main(int argc, char* argv[]) {
    if (argc == 2 && std::string(argv[1]) == "--benchmark") {
        BenchmarkSuite suite;
        suite.run();
        return 0;
    }

    if (argc != 3) {
        std::cerr << "Usage: ./simplify <input_file.csv> <target_vertices>\n";
        std::cerr << "       ./simplify --benchmark\n";
        return 1;
    }

    std::string input_file = argv[1];
    int target_vertices = std::stoi(argv[2]);

    long baseline_mem = get_peak_memory_kb();

    // Load Phase
    auto load_start = std::chrono::high_resolution_clock::now();
    std::vector<Ring> polygon;
    try { polygon = load_polygon_from_csv(input_file); }
    catch (const std::exception& e) { std::cerr << e.what() << "\n"; return 1; }
    auto load_end = std::chrono::high_resolution_clock::now();
    double load_time = std::chrono::duration<double>(load_end - load_start).count();

    int start_v = 0;
    for (const auto& ring : polygon) start_v += ring.active_vertex_count;
    double input_area = calculate_total_area(polygon);

    json orig_vals = json::array();
    for (const auto& ring : polygon) {
        if (ring.active_vertex_count < 3) continue;
        int order = 0;
        Vertex* curr = ring.head;
        do {
            orig_vals.push_back({{"x", curr->x}, {"y", curr->y}, {"ring_id", std::to_string(ring.ring_id)}, {"order", order++}});
            curr = curr->next;
        } while (curr != ring.head);
        orig_vals.push_back({{"x", ring.head->x}, {"y", ring.head->y}, {"ring_id", std::to_string(ring.ring_id)}, {"order", order}});
    }

    // Processing Phase
    auto start_time = std::chrono::high_resolution_clock::now();
    double total_displacement = simplify_polygon(polygon, target_vertices);
    auto end_time = std::chrono::high_resolution_clock::now();
    double algo_time = std::chrono::duration<double>(end_time - start_time).count();
    long final_mem = get_peak_memory_kb();

    int end_v = 0;
    for (const auto& ring : polygon) end_v += ring.active_vertex_count;
    double output_area = calculate_total_area(polygon);

    json simp_vals = json::array();
    for (const auto& ring : polygon) {
        if (ring.active_vertex_count < 3) continue;
        int order = 0;
        Vertex* curr = ring.head;
        do {
            if (curr->is_active) {
                simp_vals.push_back({{"x", curr->x}, {"y", curr->y}, {"ring_id", std::to_string(ring.ring_id)}, {"order", order++}});
            }
            curr = curr->next;
        } while (curr != ring.head);
        
        Vertex* first = ring.head;
        while (first && !first->is_active && first->next != ring.head) first = first->next;
        if (first && first->is_active) {
            simp_vals.push_back({{"x", first->x}, {"y", first->y}, {"ring_id", std::to_string(ring.ring_id)}, {"order", order}});
        }
    }

    // Visual & Console Output
    export_presentation_html(orig_vals, simp_vals, "visualization.html", input_area, output_area, total_displacement, algo_time, final_mem, start_v, end_v, load_time, baseline_mem);

    std::cout << "ring_id,vertex_id,x,y\n";
    int out_r = 0;
    for (const auto& ring : polygon) {
        if (ring.active_vertex_count < 3) continue;
        int out_v = 0;
        Vertex* curr = ring.head;
        do {
            if (curr->is_active) std::cout << out_r << "," << out_v++ << "," << curr->x << "," << curr->y << "\n";
            curr = curr->next;
        } while (curr != ring.head);
        out_r++;
    }

    std::cout << std::scientific << std::setprecision(6);
    std::cout << "Total signed area in input: " << input_area << "\n";
    std::cout << "Total signed area in output: " << output_area << "\n";
    std::cout << "Total area displacement: " << total_displacement << "\n";

    for (auto& ring : polygon) ring.cleanup();
    return 0;
}