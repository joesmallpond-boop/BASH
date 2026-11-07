cmake_minimum_required(VERSION 3.19)
set(C74_SUPPORT_DIR "${MAX_SDK_BASE}/c74support")
set(AUTHOR_DOMAIN "com.kalabash")
set(BUNDLE_IDENTIFIER "bash")
set(COPYRIGHT_STRING "Joe Caple")
set(PACKAGE_VERSION "1.0.0")
set(EXCLUDE_FROM_COLLECTIVES "no")
include(${MAX_SDK_BASE}/script/max-pretarget.cmake)

include_directories("${C74_INCLUDES}")
add_library(${PROJECT_NAME} MODULE bash_tilde.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE bash_core)

include(${MAX_SDK_BASE}/script/max-posttarget.cmake)
