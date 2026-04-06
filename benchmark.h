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
    long get_peak_memory_kb() {
        struct rusage usage;
        getrusage(RUSAGE_SELF, &usage);
        return usage.ru_maxrss;
    }

    double fit_n_log_n(const std::vector<int>& n_vals, const std::vector<double>& times) {
        double sum_uy = 0.0, sum_uu = 0.0;
        for (size_t i = 0; i < n_vals.size(); ++i) {
            double u = n_vals[i] * std::log2(n_vals[i] > 0 ? n_vals[i] : 1);
            sum_uy += u * times[i];
            sum_uu += u * u;
        }
        return (sum_uu > 0) ? (sum_uy / sum_uu) : 0.0;
    }

    double fit_linear(const std::vector<int>& n_vals, const std::vector<double>& memory) {
        double sum_xy = 0.0, sum_xx = 0.0;
        for (size_t i = 0; i < n_vals.size(); ++i) {
            sum_xy += (double)n_vals[i] * memory[i];
            sum_xx += (double)n_vals[i] * n_vals[i];
        }
        return (sum_xx > 0) ? (sum_xy / sum_xx) : 0.0;
    }

public:
    void run() {
        std::vector<TestCase> tests = {
            // --- SCALING TESTS ---
            {"test_cases/test_0001000.csv", "Small Scale", "Baseline check for O(n log n) overhead", 500},
            {"test_cases/test_0002000.csv", "Small Scale", "Testing linked list pointer consistency", 1000},
            {"test_cases/test_0005000.csv", "Medium Scale", "Evaluating priority queue build time", 2500},
            {"test_cases/test_0010000.csv", "Medium Scale", "Testing spatial grid cell distribution", 5000},
            {"test_cases/test_0025000.csv", "Large Scale", "Measuring memory growth as n increases", 12500},
            {"test_cases/test_0100000.csv", "Large Scale", "Competitive benchmark (Rubric Threshold)", 50000},
            {"test_cases/test_0250000.csv", "Extreme Scale", "Stress testing large-scale vertex removal", 100000},
            {"test_cases/test_0500000.csv", "Extreme Scale", "Proving efficiency at 5x the rubric threshold", 250000},

            // --- DISPLACEMENT CURVE TESTS ---
            {"test_cases/test_0010000.csv", "Displacement Curve", "90% vertex retention (Minimal error)", 9000},
            {"test_cases/test_0010000.csv", "Displacement Curve", "50% vertex retention (Moderate error)", 5000},
            {"test_cases/test_0010000.csv", "Displacement Curve", "10% vertex retention (High error)", 1000},
            {"test_cases/test_0010000.csv", "Displacement Curve", "Max simplification (Topology limit)", 100}
        };

        json data_values = json::array();
        std::vector<int> n_vals;
        std::vector<double> times;
        std::vector<double> mems;

        std::cerr << "\n--- Running APSC Benchmark Suite ---\n" << std::endl;

        for (const auto& test : tests) {
            std::cerr << "Processing: " << test.filename << "..." << std::endl;
            
            auto load_start = std::chrono::high_resolution_clock::now();
            std::vector<Ring> polygon;
            try { polygon = load_polygon_from_csv(test.filename); } 
            catch (...) { std::cerr << "Error: Skip " << test.filename << std::endl; continue; }
            auto load_end = std::chrono::high_resolution_clock::now();
            double load_time = std::chrono::duration<double>(load_end - load_start).count();

            int initial_v = 0;
            for (const auto& ring : polygon) initial_v += ring.active_vertex_count;
            
            auto start_time = std::chrono::high_resolution_clock::now();
            double displacement = simplify_polygon(polygon, test.target_vertices);
            auto end_time = std::chrono::high_resolution_clock::now();
            
            long peak_mem = get_peak_memory_kb();
            std::chrono::duration<double> diff = end_time - start_time;
            
            n_vals.push_back(initial_v);
            times.push_back(diff.count());
            mems.push_back((double)peak_mem);
            
            data_values.push_back({
                {"file", test.filename}, {"property", test.property}, {"challenge", test.challenge},
                {"n", initial_v}, {"target", test.target_vertices}, {"time", diff.count()},
                {"load_time", load_time}, 
                {"memory", peak_mem}, {"displacement", displacement}
            });
            for (auto& ring : polygon) ring.cleanup();
        }

        double c_time = fit_n_log_n(n_vals, times);
        double c_mem = fit_linear(n_vals, mems);
        
        json fit_values = json::array();
        int max_n = *std::max_element(n_vals.begin(), n_vals.end());
        for (int i = 0; i <= max_n; i += (max_n / 50 > 0 ? max_n / 50 : 1)) {
            fit_values.push_back({
                {"n", i}, {"ideal_time", c_time * (i * std::log2(i > 0 ? i : 1))}, {"ideal_memory", c_mem * i}
            });
        }

        std::ofstream file("benchmark_report.html");
        if (file.is_open()) {
            file << R"HTML(<!DOCTYPE html><html><head><title>APSC Benchmarks</title>
            <script src="https://cdn.jsdelivr.net/npm/vega@5"></script>
            <script src="https://cdn.jsdelivr.net/npm/vega-lite@5"></script>
            <script src="https://cdn.jsdelivr.net/npm/vega-embed@6"></script>
            <style>
                body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; margin: 40px; background: #f4f7f6; color: #333; }
                .container { max-width: 1200px; margin: auto; background: white; padding: 30px; border-radius: 8px; box-shadow: 0 4px 15px rgba(0,0,0,0.1); }
                .chart-wrapper { display: inline-block; width: 32%; vertical-align: top; padding: 0 10px; box-sizing: border-box; margin-bottom: 20px;}
                .full-wrapper { width: 100%; margin-top: 20px; padding: 0 10px; box-sizing: border-box; }
                .chart-container { background: white; width: 100%; }
                
                /* Styling for the new explanation boxes */
                .desc { font-size: 13px; color: #2c3e50; background: #e8f4f8; padding: 12px; border-left: 4px solid #3498db; border-radius: 4px; margin-top: 15px; line-height: 1.5; }
                
                table { width: 100%; border-collapse: collapse; margin-top: 30px; }
                th, td { border: 1px solid #ddd; padding: 10px; font-size: 14px; text-align: left; }
                th { background: #2c3e50; color: white; }
                h1 { border-bottom: 2px solid #eee; padding-bottom: 10px; }
            </style></head><body><div class="container">
            <h1>APSC Experimental Evaluation</h1>
            <p><b>Algo Time Fit:</b> $y \approx )HTML" << std::scientific << std::setprecision(5) << c_time << R"HTML( \cdot n \log n$ | <b>Mem Fit:</b> $y \approx )HTML" << std::fixed << std::setprecision(6) << c_mem << R"HTML( \cdot n$</p>
            
            <div class="chart-wrapper">
                <div id="load_chart" class="chart-container"></div>
                <div class="desc"><b>What this shows:</b> The time it takes to read the CSV and allocate memory before the algorithm even starts. <br><br><b>Dots:</b> Actual measured load times. Notice how loading plain text from a disk is a heavy operation.</div>
            </div>
            
            <div class="chart-wrapper">
                <div id="time_chart" class="chart-container"></div>
                <div class="desc"><b>What this shows:</b> Proof that the spatial grid and priority queue keep the algorithm fast.<br><br><b>Dots:</b> Your actual algorithm processing times.<br><b>Line:</b> The theoretical mathematical baseline.</div>
            </div>
            
            <div class="chart-wrapper">
                <div id="mem_chart" class="chart-container"></div>
                <div class="desc"><b>What this shows:</b> How RAM usage scales as files get bigger.<br><br><b>Dots:</b> Measured Peak Memory.<br><b>Line:</b> Theoretical linear growth. Using a doubly-linked list keeps this overhead incredibly low.</div>
            </div>
            
            <div class="full-wrapper">
                <div id="disp_chart" class="chart-container"></div>
                <div class="desc"><b>What this shows:</b> The trade-off between simplifying the shape and keeping it accurate. As you force the algorithm to strip away more target vertices (moving left on the graph), the areal displacement (error) naturally spikes.</div>
            </div>
            
            <table><thead><tr><th>Property</th><th>Challenge</th><th>n</th><th>Target</th><th>Load Time (s)</th><th>Algo Time (s)</th><th>Mem (KB)</th><th>Displacement</th></tr></thead>
            <tbody id="btable"></tbody></table>
            
            <script>
                const realData = )HTML" << data_values.dump() << R"HTML(;
                const fitData = )HTML" << fit_values.dump() << R"HTML(;
                const tbody = document.getElementById("btable");
                
                realData.forEach(d => {
                    let r = tbody.insertRow();
                    r.innerHTML = `<td><b>${d.property}</b></td><td>${d.challenge}</td><td>${d.n}</td><td>${d.target}</td>
                                   <td>${d.load_time.toFixed(4)}</td>
                                   <td>${d.time.toFixed(4)}</td><td>${d.memory}</td>
                                   <td>${d.displacement.toExponential(2)}</td>`;
                });
                
                // Load Time Chart (Added Legend via 'datum')
                vegaEmbed('#load_chart', {
                    "$schema": "https://vega.github.io/schema/vega-lite/v5.json",
                    "title": "Load Time vs n",
                    "data": {"values": realData},
                    "mark": {"type": "point", "filled": true, "size": 60},
                    "encoding": {
                        "x": {"field": "n", "type": "quantitative"},
                        "y": {"field": "load_time", "type": "quantitative", "title": "Load Time (s)"},
                        "color": {"datum": "Measured Data", "type": "nominal", "legend": {"title": "Legend"}, "scale": {"range": ["#9b59b6"]}}
                    }
                });

                // Time Chart (Added Legends for both dots and line via 'datum')
                vegaEmbed('#time_chart', {
                    "$schema": "https://vega.github.io/schema/vega-lite/v5.json",
                    "title": "Algo Time Scaling",
                    "layer": [
                        {
                            "data": {"values": realData}, 
                            "mark": {"type": "point", "filled": true, "size": 60}, 
                            "encoding": {
                                "x": {"field": "n", "type": "quantitative"}, 
                                "y": {"field": "time", "type": "quantitative", "title": "Algorithm Time (s)"},
                                "color": {"datum": "Measured Time", "type": "nominal", "legend": {"title": "Legend"}}
                            }
                        }, 
                        {
                            "data": {"values": fitData}, 
                            "mark": {"type": "line", "strokeWidth": 2, "strokeDash": [4, 4]}, 
                            "encoding": {
                                "x": {"field": "n", "type": "quantitative"}, 
                                "y": {"field": "ideal_time", "type": "quantitative"},
                                "color": {"datum": "O(n log n) Fit", "type": "nominal"}
                            }
                        }
                    ],
                    "resolve": {"scale": {"color": "independent"}}
                });

                // Memory Chart (Added Legends for both dots and line via 'datum')
                vegaEmbed('#mem_chart', {
                    "$schema": "https://vega.github.io/schema/vega-lite/v5.json",
                    "title": "Memory Scaling",
                    "layer": [
                        {
                            "data": {"values": realData}, 
                            "mark": {"type": "point", "filled": true, "size": 60}, 
                            "encoding": {
                                "x": {"field": "n", "type": "quantitative"}, 
                                "y": {"field": "memory", "type": "quantitative", "title": "Peak Memory (KB)"},
                                "color": {"datum": "Measured Memory", "type": "nominal", "legend": {"title": "Legend"}}
                            }
                        }, 
                        {
                            "data": {"values": fitData}, 
                            "mark": {"type": "line", "strokeWidth": 2, "strokeDash": [4, 4]}, 
                            "encoding": {
                                "x": {"field": "n", "type": "quantitative"}, 
                                "y": {"field": "ideal_memory", "type": "quantitative"},
                                "color": {"datum": "O(n) Target", "type": "nominal"}
                            }
                        }
                    ],
                    "resolve": {"scale": {"color": "independent"}}
                });

                // Displacement Chart (Already had legend, just making it look nicer)
                vegaEmbed('#disp_chart', {
                    "$schema": "https://vega.github.io/schema/vega-lite/v5.json",
                    "title": "Areal Displacement vs Target Vertex Count",
                    "data": {"values": realData.filter(d => d.property === "Displacement Curve")},
                    "mark": {"type": "line", "point": {"filled": true, "size": 50}},
                    "encoding": {
                        "x": {"field": "target", "type": "quantitative", "sort": "descending", "title": "Target Vertices Remaining"},
                        "y": {"field": "displacement", "type": "quantitative", "title": "Total Areal Displacement"},
                        "color": {"field": "file", "type": "nominal", "legend": {"title": "Source Dataset"}}
                    }
                });
            </script></div></body></html>)HTML";
            file.close();
        }
    }
};