cmake_minimum_required(VERSION 3.16)
project(falconfoc_studio CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(IMGUI  ${CMAKE_SOURCE_DIR}/external/imgui)
set(IMPLOT ${CMAKE_SOURCE_DIR}/external/implot)

add_executable(falconfoc_studio
  src/main.cpp
  src/app.cpp
  src/can_bus.cpp
  # Dear ImGui core + GLFW/OpenGL3 backends
  ${IMGUI}/imgui.cpp
  ${IMGUI}/imgui_draw.cpp
  ${IMGUI}/imgui_tables.cpp
  ${IMGUI}/imgui_widgets.cpp
  ${IMGUI}/backends/imgui_impl_glfw.cpp
  ${IMGUI}/backends/imgui_impl_opengl3.cpp
  # ImPlot
  ${IMPLOT}/implot.cpp
  ${IMPLOT}/implot_items.cpp
)

target_include_directories(falconfoc_studio PRIVATE
  src ${IMGUI} ${IMGUI}/backends ${IMPLOT}
)

# --- dependencies: GLFW + OpenGL ---
find_package(OpenGL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLFW REQUIRED glfw3)

target_include_directories(falconfoc_studio PRIVATE ${GLFW_INCLUDE_DIRS})
target_link_libraries(falconfoc_studio PRIVATE
  ${GLFW_LIBRARIES} OpenGL::GL ${CMAKE_DL_LIBS} pthread
)
target_link_directories(falconfoc_studio PRIVATE ${GLFW_LIBRARY_DIRS})

# strip symbols in release for a lean single binary
set_target_properties(falconfoc_studio PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build)
