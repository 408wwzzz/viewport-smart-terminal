cmake_minimum_required(VERSION 3.10)
# 设置交叉编译
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)
# 这里指定的路径必须是绝对路径 “参考交叉开发，交叉编译器安装”
set(CMAKE_C_COMPILER /usr/local/arm-linux/bin/aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER /usr/local/arm-linux/bin/aarch64-linux-gnu-g++)
project(lvgl)

set(CMAKE_C_STANDARD 99)#C99 # lvgl officially support C99 and above
set(CMAKE_CXX_STANDARD 17)#C17
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)

add_subdirectory(lvgl)
target_include_directories(lvgl PUBLIC ${PROJECT_SOURCE_DIR})

add_executable(main main.c  mouse_cursor_icon.c src/face_change.c src/talkroom_client.c)

target_link_libraries(main lvgl lvgl::examples lvgl::demos lvgl::thorvg ${SDL2_LIBRARIES} m pthread)
target_include_directories(main PRIVATE ${PROJECT_SOURCE_DIR}/src)
add_custom_target (run COMMAND ${EXECUTABLE_OUTPUT_PATH}/main DEPENDS main)

