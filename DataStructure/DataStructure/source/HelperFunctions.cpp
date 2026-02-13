#include "HelperFunctions.h"

namespace FileLoader {
	// --- File I/O Helpers ---
	void SaveToFile(const char* filename, std::vector<Players::ID>& g_AllPlayers) {
		std::ofstream out(filename);
		if (!out.is_open()) {
			std::cerr << "Error: Could not save to " << filename << std::endl;
			return;
		}
		for (const auto& p : g_AllPlayers) {
			out << p.name << " " << p.score << "\n";
		}
		out.close();
	}

	void LoadFromFile(const char* filename, std::vector<Players::ID>& g_AllPlayers) {
		std::ifstream in(filename);
		if (!in.is_open()) {
			std::cerr << "Error: Could not find " << filename << std::endl;
			return;
		}
		g_AllPlayers.clear();
		Players::ID temp;
		while (in >> temp.name >> temp.score) {
			g_AllPlayers.push_back(temp);
		}
		in.close();
	}


}

namespace QuickSortAlgo{

	int Partition(std::vector<Players::ID>& arr, int low, int high) {
		int pivotScore = arr[high].score;
		int i = (low - 1);
		for (int j = low; j <= high - 1; j++) {
			if (arr[j].score > pivotScore) {
				i++;
				std::swap(arr[i], arr[j]);
			}
		}
		std::swap(arr[i + 1], arr[high]);
		return (i + 1);
	}

	void StartSortSimulation(SortState& g_Sort, const std::vector<Players::ID>& g_AllIDs) {
		g_Sort.data = g_AllIDs;
		while (!g_Sort.jobStack.empty()) g_Sort.jobStack.pop();
		g_Sort.currentLow = -1;
		g_Sort.currentHigh = -1;
		g_Sort.lastPivotIdx = -1;
		g_Sort.timer = 0.0f;

		if (g_Sort.data.size() > 1) {
			g_Sort.jobStack.push({ 0, (int)g_Sort.data.size() - 1 });
			g_Sort.isSorting = true;
		}
	}
}
