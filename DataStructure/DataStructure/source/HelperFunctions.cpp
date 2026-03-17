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

	// Lomuto Partition Scheme modified for Descending Order
	int Partition(std::vector<Players::ID>& arr, int low, int high) {
		// Choose the last element in the current range as the pivot
		int pivotScore = arr[high].score;

		// 'i' tracks the boundary of the "Greater than Pivot" section
		int i = (low - 1);

		// Iterate from the start to the element just before the pivot
		for (int j = low; j <= high - 1; j++) {
			// If current element is larger than the pivot, move it to the 'left' (front)
			if (arr[j].score > pivotScore) {
				i++;
				std::swap(arr[i], arr[j]);
			}
		}

		// Move the pivot from the end to its correct sorted position (i + 1)
		std::swap(arr[i + 1], arr[high]);

		// Return the pivot's final index to split the next 'jobs' in the stack
		return (i + 1);
	}

	/**
	 * Initializes the sorting state to begin a timed, visual Quicksort.
	 * This sets the "initial conditions" for the iterative stack-based approach.
	 */
	void StartSortSimulation(SortState& g_Sort, const std::vector<Players::ID>& g_AllIDs) {
		// 1. Create a local copy of the data so we don't modify the original list 
		//    until the sorting process is fully complete.
		g_Sort.data = g_AllIDs;

		// 2. Clear the Job Stack to ensure no leftover ranges from a previous sort remain.
		while (!g_Sort.jobStack.empty()) g_Sort.jobStack.pop();

		// 3. Reset visualization markers (indices used for highlighting in the UI).
		g_Sort.currentLow = -1;
		g_Sort.currentHigh = -1;
		g_Sort.lastPivotIdx = -1;

		// 4. Reset the timer so the first step triggers after exactly 'sortSpeed' seconds.
		g_Sort.timer = 0.0f;

		// 5. Safety check: Only start sorting if there is more than one element.
		if (g_Sort.data.size() > 1) {
			// PUSH THE INITIAL JOB:
			// In Quicksort, we start by looking at the entire range (from index 0 to N-1).
			// This 'push' mimics the first call in a recursive function: QuickSort(arr, 0, n-1).
			g_Sort.jobStack.push({ 0, (int)g_Sort.data.size() - 1 });

			// Set the flag to true so the update loop begins processing the stack.
			g_Sort.isSorting = true;
		}
	}
}
