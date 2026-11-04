cmake_minimum_required(VERSION 3.22.1)
project("oxide")

# 1. Инжектор
add_executable(injector_bin injector_bin.cpp)
target_link_libraries(injector_bin log dl)
set_target_properties(injector_bin PROPERTIES OUTPUT_NAME "injector_bin.so")

# 2. Настройка Dobby
set(DOBBY_SOURCE_DIR Dobby)
set(DOBBY_GENERATE_SHARED OFF CACHE BOOL "" FORCE)
set(DOBBY_DEBUG OFF CACHE BOOL "" FORCE)

# Добавляем Dobby как подпроект (это решает проблему с ассемблером)
add_subdirectory(${DOBBY_SOURCE_DIR} dobby.out)

# 3. Меню (ImGui + Dobby)
add_library(modmenu SHARED modmenu.cpp 
    imgui/imgui.cpp imgui/imgui_draw.cpp imgui/imgui_tables.cpp imgui/imgui_widgets.cpp
    imgui/backends/imgui_impl_vulkan.cpp imgui/backends/imgui_impl_android.cpp
)

target_include_directories(modmenu PRIVATE imgui)

find_library(log-lib log)
find_library(android-lib android)
find_library(vulkan-lib vulkan)

# Линкуем с Dobby
target_link_libraries(modmenu ${log-lib} ${android-lib} ${vulkan-lib} dl dobby)
