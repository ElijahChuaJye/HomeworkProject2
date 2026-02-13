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


namespace QuickSortAlgo{
	// --- Data Structures ---
	struct SortState {
		std::vector<Players::ID> data;
		std::stack<std::pair<int, int>> jobStack;
		bool isSorting = false;
		float timer = 0.0f;
		int currentLow = -1;
		int currentHigh = -1;
		int lastPivotIdx = -1;
	};

	int Partition(std::vector<Players::ID>& arr, int low, int high);

	void StartSortSimulation(SortState& gSort, const std::vector<Players::ID>&);
}

#endif