#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <fstream>
#include <stack>
#include <stdio.h>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>

// --- Data Structures ---

struct Player {
    char name[64];
    int score;
};

struct SortState {
    std::vector<Player> data;
    std::stack<std::pair<int, int>> jobStack;
    bool isSorting = false;
    float timer = 0.0f;
    int currentLow = -1;
    int currentHigh = -1;
    int lastPivotIdx = -1;
};

// --- Global State ---
std::vector<Player> g_AllPlayers;
SortState g_Sort;

// --- File I/O Helpers ---
void SaveToFile(const char* filename) {
    std::ofstream out(filename);
    if (!out.is_open()) return;
    for (const auto& p : g_AllPlayers) {
        out << p.name << " " << p.score << "\n";
    }
    out.close();
}

void LoadFromFile(const char* filename) {
    std::ifstream in(filename);
    if (!in.is_open()) return;
    g_AllPlayers.clear();
    Player temp;
    while (in >> temp.name >> temp.score) {
        g_AllPlayers.push_back(temp);
    }
    in.close();
}

// --- Quick Sort Logic ---
int Partition(std::vector<Player>& arr, int low, int high) {
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

void StartSortSimulation() {
    g_Sort.data = g_AllPlayers;
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
    GLFWwindow* window = glfwCreateWindow(1280, 720, "Quick Sort Visualizer", nullptr, nullptr);
    if (window == nullptr) return 1;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    bool show_demo_window = false;
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    static char inputName[64] = "";
    static int inputScore = 0;
    static float sortSpeed = 0.5f;

    // State for editing
    static int selectedPlayerIndex = -1;
    static int editScoreBuffer = 0;

    LoadFromFile("scores.txt");

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
                    int pi = Partition(g_Sort.data, low, high);
                    g_Sort.lastPivotIdx = pi;
                    if (pi - 1 > low) g_Sort.jobStack.push({ low, pi - 1 });
                    if (pi + 1 < high) g_Sort.jobStack.push({ pi + 1, high });
                }
                else {
                    g_Sort.isSorting = false;
                    g_Sort.currentLow = -1;
                    g_Sort.currentHigh = -1;
                    g_AllPlayers = g_Sort.data;
                    SaveToFile("scores.txt");
                }
            }
        }

        // --- GUI RENDER ---
        {
            ImGui::Begin("High Score Manager");

            // Input Section
            ImGui::Text("Add New Player");
            ImGui::InputText("Name", inputName, 64);
            ImGui::SliderInt("Score", &inputScore, 0, 100);

            if (ImGui::Button("Add Player")) {
                Player p;
                snprintf(p.name, 64, "%s", inputName);
                p.score = inputScore;
                g_AllPlayers.push_back(p);
                inputName[0] = '\0';
                inputScore = 0;
                SaveToFile("scores.txt");
            }

            ImGui::SameLine();
            if (ImGui::Button("Load from File")) LoadFromFile("scores.txt");

            ImGui::Separator();
            ImGui::Text("Simulation Controls");
            ImGui::SliderFloat("Speed (Sec/Step)", &sortSpeed, 0.05f, 2.0f);

            if (ImGui::Button(g_Sort.isSorting ? "Sorting..." : "Start Quick Sort")) {
                StartSortSimulation();
            }

            ImGui::Separator();
            ImGui::Text("Live Visualization (Click bar to edit):");

            std::vector<Player>& displayList = g_Sort.isSorting ? g_Sort.data : g_AllPlayers;

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

                // Unique ID for each button so ImGui doesn't get confused
                std::string btnId = "##btn" + std::to_string(i);

                // The button covers the full possible width so it's easy to click
                if (ImGui::InvisibleButton(btnId.c_str(), ImVec2(maxWidth, barHeight))) {
                    if (!g_Sort.isSorting) { // Prevent editing during active sort
                        selectedPlayerIndex = i;
                        editScoreBuffer = displayList[i].score;
                        ImGui::OpenPopup("Edit Player Score");
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
            if (ImGui::BeginPopupModal("Edit Player Score", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
                if (selectedPlayerIndex >= 0 && selectedPlayerIndex < g_AllPlayers.size()) {
                    ImGui::Text("Editing: %s", g_AllPlayers[selectedPlayerIndex].name);
                    ImGui::SliderInt("New Score", &editScoreBuffer, 0, 100);

                    if (ImGui::Button("Save", ImVec2(120, 0))) {
                        g_AllPlayers[selectedPlayerIndex].score = editScoreBuffer;
                        SaveToFile("scores.txt");
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                        ImGui::CloseCurrentPopup();
                    }
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