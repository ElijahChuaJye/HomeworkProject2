#include <iostream>
#include <stdio.h>

#include "HelperFunctions.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>



// --- Global State ---
std::vector<Players::ID> g_AllIDs;
QuickSortAlgo::SortState g_Sort;
const char* LOAD_PATH = "TextDocs/unsorted_scores.txt"; // The source file
const char* SAVE_PATH = "TextDocs/sorted_scores.txt";   // The output file


// --- Quick Sort Logic ---


static void glfw_error_callback(int error, const char* description) {
	fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

// --- Main ---
int main() {
	glfwSetErrorCallback(glfw_error_callback);
	if (!glfwInit()) return 1;
	const char* glsl_version = "#version 130";
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
	GLFWwindow* window = glfwCreateWindow(1300, 800, "Quick Sort Visualizer", nullptr, nullptr);
	if (window == nullptr) return 1;
	glfwMakeContextCurrent(window);
	glfwSwapInterval(1);

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); 
	(void)io;
	ImGui::StyleColorsDark();
	ImGui::SetNextWindowSize(ImVec2(1200, 700));
	ImGui_ImplGlfw_InitForOpenGL(window, true);
	ImGui_ImplOpenGL3_Init(glsl_version);

	bool show_demo_window = false;
	ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

	static char inputName[64] = "";
	static int inputScore = 0;
	static float sortSpeed = 0.5f;

	// State for editing
	static int selectedIDIndex = -1;
	static int editScoreBuffer = 0;

	FileLoader::LoadFromFile(LOAD_PATH, g_AllIDs);

	while (!glfwWindowShouldClose(window)) {
		glfwPollEvents();
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		// --- SORTING LOGIC ---
		if (g_Sort.isSorting) {
			g_Sort.timer += io.DeltaTime;
			if (g_Sort.timer >= sortSpeed) {
				g_Sort.timer = 0.0f;
				if (!g_Sort.jobStack.empty()) {
					std::pair<int, int> range = g_Sort.jobStack.top();
					g_Sort.jobStack.pop();
					int low = range.first;
					int high = range.second;
					g_Sort.currentLow = low;
					g_Sort.currentHigh = high;
					int pi = QuickSortAlgo::Partition(g_Sort.data, low, high);
					g_Sort.lastPivotIdx = pi;
					if (pi - 1 > low) g_Sort.jobStack.push({ low, pi - 1 });
					if (pi + 1 < high) g_Sort.jobStack.push({ pi + 1, high });
				}
				else {
					g_Sort.isSorting = false;
					g_Sort.currentLow = -1;
					g_Sort.currentHigh = -1;
					g_AllIDs = g_Sort.data;
					FileLoader::SaveToFile(SAVE_PATH, g_AllIDs);
				}
			}
		}

		// --- GUI RENDER ---
		{
			
			ImGui::Begin("High Score Manager");

			// Input Section
			ImGui::Text("Add New ID");
			ImGui::InputText("Name", inputName, 64);

			ImGui::SliderInt("Score", &inputScore, 0, 100);

			if (ImGui::Button("Add Players")) {
				Players::ID p;
				snprintf(p.name, 64, "%s", inputName);
				p.score = inputScore;
				g_AllIDs.push_back(p);
				inputName[0] = '\0';
				inputScore = 0;
				FileLoader::SaveToFile(SAVE_PATH, g_AllIDs);
			}

			ImGui::SameLine();
			if (ImGui::Button("Load from File")) FileLoader::LoadFromFile(LOAD_PATH, g_AllIDs);

			ImGui::Separator();
			ImGui::Text("Simulation Controls");
			ImGui::SliderFloat("Speed (Sec/Step)", &sortSpeed, 0.05f, 2.0f);

			if (ImGui::Button(g_Sort.isSorting ? "Sorting..." : "Start Quick Sort")) {
				QuickSortAlgo::StartSortSimulation(g_Sort, g_AllIDs);
			}

			ImGui::Separator();
			ImGui::Text("Live Visualization (Click bar to edit):");

			std::vector<Players::ID>& displayList = g_Sort.isSorting ? g_Sort.data : g_AllIDs;

			ImDrawList* draw_list = ImGui::GetWindowDrawList();
			ImVec2 p = ImGui::GetCursorScreenPos();
			float startX = p.x;
			float startY = p.y;
			float barHeight = 20.0f;
			float maxWidth = 300.0f;

			for (int i = 0; i < displayList.size(); i++) {
				float y = startY + (i * (barHeight + 5));

				// 1. Determine Color
				ImU32 color = IM_COL32(100, 100, 100, 255);
				if (g_Sort.isSorting) {
					if (i == g_Sort.lastPivotIdx) color = IM_COL32(0, 255, 0, 255);
					else if (i >= g_Sort.currentLow && i <= g_Sort.currentHigh) color = IM_COL32(255, 0, 0, 255);
				}

				// 2. Calculate Geometry
				float width = ((float)displayList[i].score / 100.0f) * maxWidth;
				if (width < 5.0f) width = 5.0f;
				if (width > maxWidth) width = maxWidth;

				// 3. Draw Bar
				draw_list->AddRectFilled(ImVec2(startX, y), ImVec2(startX + width, y + barHeight), color);

				// 4. Draw Text
				char label[128];
				snprintf(label, 128, "%s (%d)", displayList[i].name, displayList[i].score);
				draw_list->AddText(ImVec2(startX + 10, y + 2), IM_COL32(255, 255, 255, 255), label);

				// 5. CLICK INTERACTION (New Logic)
				// We create an InvisibleButton exactly where the bar is.
				ImGui::SetCursorScreenPos(ImVec2(startX, y));

				// Unique Players::ID for each button so ImGui doesn't get confused
				std::string btnId = "##btn" + std::to_string(i);

				// The button covers the full possible width so it's easy to click
				if (ImGui::InvisibleButton(btnId.c_str(), ImVec2(maxWidth, barHeight))) {
					if (!g_Sort.isSorting) { // Prevent editing during active sort
						selectedIDIndex = i;
						editScoreBuffer = displayList[i].score;
						ImGui::OpenPopup("Edit Players Score");
					}
				}

				// Hover effect: Draw a white border if hovered
				if (ImGui::IsItemHovered()) {
					draw_list->AddRect(ImVec2(startX, y), ImVec2(startX + width, y + barHeight), IM_COL32(255, 255, 255, 255));
				}
			}

			// Advance layout cursor
			ImGui::Dummy(ImVec2(0, (displayList.size() * (barHeight + 5))));

			// --- POPUP WINDOW FOR EDITING ---
			if (ImGui::BeginPopupModal("Edit Players Score", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
				if (selectedIDIndex >= 0 && selectedIDIndex < (int)g_AllIDs.size()) {
					ImGui::Text("Editing: %s", g_AllIDs[selectedIDIndex].name);
					ImGui::SliderInt("New Score", &editScoreBuffer, 0, 100);
					ImGui::Separator();

					// Line 1: Save and Cancel
					if (ImGui::Button("Save", ImVec2(120, 0))) {
						g_AllIDs[selectedIDIndex].score = editScoreBuffer;
						FileLoader::SaveToFile(SAVE_PATH, g_AllIDs);
						ImGui::CloseCurrentPopup();
					}
					ImGui::SameLine();
					if (ImGui::Button("Cancel", ImVec2(120, 0))) {
						ImGui::CloseCurrentPopup();
					}

					ImGui::Spacing();

					// Line 2: The Big Red Remove Button
					const char* targetName = g_AllIDs[selectedIDIndex].name;
					char buf[128];
					snprintf(buf, sizeof(buf), "Remove %s", targetName);

					ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.1f, 0.1f, 1.0f));
					// Use 247 width to match the two buttons above combined (120 + 120 + padding)
					if (ImGui::Button(buf, ImVec2(247, 0))) {
						g_AllIDs.erase(g_AllIDs.begin() + selectedIDIndex);
						FileLoader::SaveToFile(SAVE_PATH, g_AllIDs);
						selectedIDIndex = -1;
						ImGui::CloseCurrentPopup();
					}
					ImGui::PopStyleColor();
				}
				else {
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}

			ImGui::End();
		}

		ImGui::Render();
		int display_w, display_h;
		glfwGetFramebufferSize(window, &display_w, &display_h);
		glViewport(0, 0, display_w, display_h);
		glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
		glClear(GL_COLOR_BUFFER_BIT);
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
		glfwSwapBuffers(window);
	}

	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();
	glfwDestroyWindow(window);
	glfwTerminate();

	return 0;
}