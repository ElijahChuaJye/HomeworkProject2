#ifndef HELPERFUNCTIONS_HPP
#define HELPERFUNCTIONS_HPP
#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <algorithm>
#include <stack>

namespace Players {
	struct ID {
		char name[64];
		int score;
	};

}

namespace FileLoader {

	void SaveToFile(const char* filename, std::vector<Players::ID>&);

	void LoadFromFile(const char* filename, std::vector<Players::ID>&);

}


namespace QuickSortAlgo {
    // --- Data Structures ---
    struct SortState {
        std::vector<Players::ID> data;       // Local copy of data being sorted
        std::stack<std::pair<int, int>> jobStack; // Replaces recursion; stores pending [low, high] ranges
        bool isSorting = false;              // Toggle for the update loop
        float timer = 0.0f;                  // Tracks time elapsed between sort steps
        int currentLow = -1;                 // Visualizing: the start of the current segment
        int currentHigh = -1;                // Visualizing: the end of the current segment (pivot source)
        int lastPivotIdx = -1;               // Visualizing: where the pivot landed
    };

    // Rearranges the array around a pivot so: [elements > pivot | pivot | elements < pivot]
    int Partition(std::vector<Players::ID>& arr, int low, int high);

    // Initializes the SortState and pushes the initial full-array range {0, n-1} onto the stack
    void StartSortSimulation(SortState& gSort, const std::vector<Players::ID>&);
}

#endif