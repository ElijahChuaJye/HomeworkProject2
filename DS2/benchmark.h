#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <sys/resource.h>
#include <cmath>
#include <fstream>
#include <algorithm>
#include "geometry.h"
#include "apsc.h"
#include "json.hpp" 

using json = nlohmann::json;

struct TestCase {
    std::string filename;
    std::string property;
    std::string challenge;
    int target_vertices;
};

class BenchmarkSuite {
private:
    // Helper to get peak resident set size (memory) in KB
    long get_peak_memory_kb() {
        struct rusage usage;
        getrusage(RUSAGE_SELF, &usage);
        return usage.ru_maxrss;
    }

    // Least Squares Fit for y = c * (n * log2(n))
    double fit_n_log_n(const std::vector<int>& n_vals, const std::vector<double>& times) {
        double sum_uy = 0.0, sum_uu = 0.0;
        for (size_t i = 0; i < n_vals.size(); ++i) {
            double u = n_vals[i] * std::log2(n_vals[i] > 0 ? n_vals[i] : 1);
            sum_uy += u * times[i];
            sum_uu += u * u;
        }
        return (sum_uu > 0) ? (sum_uy / sum_uu) : 0.0;
    }

    // Least Squares Fit for y = c * n (Linear for Memory)
    double fit_linear(const std::vector<int>& n_vals, const std::vector<double>& memory) {
        double sum_xy = 0.0, sum_xx = 0.0;
        for (size_t i = 0; i < n_vals.size(); ++i) {
            sum_xy += (double)n_vals[i] * memory[i];
            sum_xx += (double)n_vals[i] * n_vals[i];
        }
        return (sum_xx > 0) ? (sum_xy / sum_xx) : 0.0;
    }

public:
    void run_and_export_html(const std::vector<TestCase>& tests) {
        json data_values = json::array();
        std::vector<int> n_vals;
        std::vector<double> times;
        std::vector<double> mems;

        std::cerr << "\n--- Running APSC Benchmark Suite ---\n" << std::endl;

        for (const auto& test : tests) {
            std::cerr << "Processing: " << test.filename << " (Target: " << test.target_vertices << ")..." << std::endl;
            
            // 1. Load the polygon from the CSV [cite: 78, 82]
            std::vector<Ring> polygon;
            try {
                polygon = load_polygon_from_csv(test.filename);
            } catch (...) {
                std::cerr << "Error: Failed to load " << test.filename << std::endl;
                continue;
            }

            int initial_v = 0;
            for (const auto& ring : polygon) initial_v += ring.active_vertex_count;
            
            // 2. Benchmarking the core APSC algorithm [cite: 20, 138]
            auto start_time = std::chrono::high_resolution_clock::now();
            double displacement = simplify_polygon(polygon, test.target_vertices);
            auto end_time = std::chrono::high_resolution_clock::now();
            
            long peak_mem = get_peak_memory_kb();
            std::chrono::duration<double> diff = end_time - start_time;
            
            // 3. Store results for curve fitting [cite: 184, 185]
            n_vals.push_back(initial_v);
            times.push_back(diff.count());
            mems.push_back((double)peak_mem);
            
            data_values.push_back({
                {"file", test.filename},
                {"property", test.property},
                {"challenge", test.challenge},
                {"n", initial_v},
                {"target", test.target_vertices},
                {"time", diff.count()},
                {"memory", peak_mem},
                {"displacement", displacement}
            });
            
            // Cleanup to maintain accurate peak memory per test [cite: 22, 170]
            for (auto& ring : polygon) ring.cleanup();
        }

        if (n_vals.empty()) return;

        // 4. Perform Asymptotic Reasoning (Fitting) [cite: 28, 185]
        double c_time = fit_n_log_n(n_vals, times);
        double c_mem = fit_linear(n_vals, mems);
        
        json fit_values = json::array();
        int max_n = *std::max_element(n_vals.begin(), n_vals.end());
        for (int i = 0; i <= max_n; i += (max_n / 50 > 0 ? max_n / 50 : 1)) {
            fit_values.push_back({
                {"n", i},
                {"ideal_time", c_time * (i * std::log2(i > 0 ? i : 1))},
                {"ideal_memory", c_mem * i}
            });
        }

        // 5. Generate the Rubric-compliant HTML Report [cite: 181, 184]
        std::ofstream file("benchmark_report.html");
        if (file.is_open()) {
            file << R"HTML(<!DOCTYPE html>
<html>
<head>
  <title>APSC Algorithm Benchmarks</title>
  <script src="https://cdn.jsdelivr.net/npm/vega@5.25.0"></script>
  <script src="https://cdn.jsdelivr.net/npm/vega-lite@5.15.0"></script>
  <script src="https://cdn.jsdelivr.net/npm/vega-embed@6.22.2"></script>
  <style>
    body { font-family: Arial, sans-serif; margin: 40px; background-color: #f9f9f9; }
    .chart { display: inline-block; width: 45%; margin: 20px; background: white; padding: 15px; border-radius: 8px; box-shadow: 0 2px 5px rgba(0,0,0,0.1); }
    table { width: 100%; border-collapse: collapse; margin-top: 20px; background: white; }
    th, td { border: 1px solid #ccc; padding: 10px; text-align: left; }
    th { background-color: #333; color: white; }
    tr:nth-child(even) { background-color: #f2f2f2; }
    h1 { color: #2c3e50; }
  </style>
</head>
<body>
  <h1>Algorithm Evaluation Report (CSD2183 Project 2)</h1>
  <p><strong>Time Complexity Fit:</strong> $y \approx )HTML" << c_time << R"HTML( \cdot (n \log_2 n)$</p>
  <p><strong>Space Complexity Fit:</strong> $y \approx )HTML" << c_mem << R"HTML( \cdot n$</p>
  
  <div id="time_chart" class="chart"></div>
  <div id="mem_chart" class="chart"></div>
  <div id="disp_chart" class="chart" style="width: 93%;"></div>
  
  <h2>Experimental Results Table</h2>
  <table id="data_table">
    <thead>
      <tr><th>File</th><th>Property</th><th>Challenge</th><th>Vertices (n)</th><th>Time (s)</th><th>Peak Mem (KB)</th><th>Areal Displacement</th></tr>
    </thead>
    <tbody></tbody>
  </table>

  <script>
    const realData = )HTML" << data_values.dump() << R"HTML(;
    const fitData = )HTML" << fit_values.dump() << R"HTML(;
    
    const tableBody = document.querySelector("#data_table tbody");
    realData.forEach(d => {
        let row = tableBody.insertRow();
        row.innerHTML = `<td>${d.file}</td><td>${d.property}</td><td>${d.challenge}</td>
                         <td>${d.n}</td><td>${d.time.toFixed(5)}</td>
                         <td>${d.memory}</td><td>${d.displacement.toExponential(4)}</td>`;
    });

    vegaEmbed('#time_chart', {
      "$schema": "https://vega.github.io/schema/vega-lite/v5.json",
      "title": "(a) Running Time vs Input Size",
      "layer": [
        {"data": {"values": realData}, "mark": {"type": "point", "filled": true, "size": 100}, "encoding": {"x": {"field": "n", "type": "quantitative"}, "y": {"field": "time", "type": "quantitative", "title": "Time (seconds)"}, "tooltip": [{"field": "file", "type": "nominal"}, {"field": "challenge", "type": "nominal"}]}},
        {"data": {"values": fitData}, "mark": {"type": "line", "color": "#e74c3c"}, "encoding": {"x": {"field": "n", "type": "quantitative"}, "y": {"field": "ideal_time", "type": "quantitative"}}}
      ]
    });

    vegaEmbed('#mem_chart', {
      "$schema": "https://vega.github.io/schema/vega-lite/v5.json",
      "title": "(b) Peak Memory vs Input Size",
      "layer": [
        {"data": {"values": realData}, "mark": {"type": "point", "filled": true, "size": 100}, "encoding": {"x": {"field": "n", "type": "quantitative"}, "y": {"field": "memory", "type": "quantitative", "title": "Memory (KB)"}, "tooltip": [{"field": "file", "type": "nominal"}]}},
        {"data": {"values": fitData}, "mark": {"type": "line", "color": "#f39c12"}, "encoding": {"x": {"field": "n", "type": "quantitative"}, "y": {"field": "ideal_memory", "type": "quantitative"}}}
      ]
    });

    vegaEmbed('#disp_chart', {
      "$schema": "https://vega.github.io/schema/vega-lite/v5.json",
      "title": "(c) Areal Displacement vs Target Vertices",
      "data": {"values": realData},
      "mark": {"type": "line", "point": true},
      "encoding": {
        "x": {"field": "target", "type": "quantitative", "title": "Target Vertex Count"},
        "y": {"field": "displacement", "type": "quantitative", "title": "Areal Displacement"},
        "color": {"field": "file", "type": "nominal"}
      }
    });
  </script>
</body>
</html>)HTML";
            file.close();
            std::cerr << "\n[SUCCESS] Benchmark Report Generated: benchmark_report.html\n" << std::endl;
        } else {
            std::cerr << "[ERROR] Could not write to benchmark_report.html" << std::endl;
        }
    }
};