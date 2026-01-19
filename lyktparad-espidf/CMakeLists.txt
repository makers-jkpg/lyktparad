cmake_minimum_required(VERSION 3.16.0)
# Disable git revision detection to prevent build errors when git metadata is unavailable
# This must be set before including project.cmake
set(GIT_FOUND FALSE CACHE BOOL "Git found" FORCE)
# Set default project version to prevent git revision detection errors
# This will be overridden by git describe if git is available
if(NOT DEFINED PROJECT_VER)
    set(PROJECT_VER "1.0.0" CACHE STRING "Project version" FORCE)
endif()
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(lyktparad-espidf)
