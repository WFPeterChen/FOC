// FalconFOC Studio — entry point. GLFW + OpenGL3 + Dear ImGui + ImPlot.
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"
#include "app.h"
#include <GLFW/glfw3.h>
#include <cstdio>

static void glfw_err(int e, const char* d) { std::fprintf(stderr, "GLFW %d: %s\n", e, d); }

static void apply_dark_theme() {
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 5; s.FrameRounding = 4; s.GrabRounding = 4;
    s.ScrollbarRounding = 4; s.WindowBorderSize = 1; s.FrameBorderSize = 0;
    s.WindowPadding = ImVec2(10,10); s.ItemSpacing = ImVec2(8,7);
    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]      = ImVec4(0.09f,0.10f,0.12f,1.00f);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.13f,0.18f,0.24f,1.00f);
    c[ImGuiCol_FrameBg]       = ImVec4(0.16f,0.18f,0.21f,1.00f);
    c[ImGuiCol_FrameBgHovered]= ImVec4(0.22f,0.26f,0.31f,1.00f);
    c[ImGuiCol_Button]        = ImVec4(0.20f,0.24f,0.29f,1.00f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.28f,0.34f,0.42f,1.00f);
    c[ImGuiCol_Header]        = ImVec4(0.20f,0.26f,0.33f,1.00f);
    c[ImGuiCol_SliderGrab]    = ImVec4(0.30f,0.55f,0.85f,1.00f);
    c[ImGuiCol_CheckMark]     = ImVec4(0.35f,0.70f,1.00f,1.00f);
}

int main() {
    glfwSetErrorCallback(glfw_err);
    if (!glfwInit()) { std::fprintf(stderr, "glfwInit failed\n"); return 1; }
    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    GLFWwindow* win = glfwCreateWindow(1280, 840, "FalconFOC Studio", nullptr, nullptr);
    if (!win) { std::fprintf(stderr, "window create failed\n"); glfwTerminate(); return 1; }
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1); // vsync

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = "falconfoc_studio.ini"; // remember window layout
    apply_dark_theme();
    ImGui_ImplGlfw_InitForOpenGL(win, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    App app;

    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        app.render();

        ImGui::Render();
        int w, h; glfwGetFramebufferSize(win, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.05f, 0.06f, 0.07f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(win);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
