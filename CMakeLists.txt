cmake_minimum_required(VERSION 3.30)

project(bedschem LANGUAGES CXX)

include(cmake/warnings.cmake)

file(GLOB_RECURSE bedschem_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
file(GLOB_RECURSE bedschem_headers CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h")

add_library(bedschem STATIC)
add_library(bedschem::bedschem ALIAS bedschem)

target_sources(bedschem
	PRIVATE ${bedschem_sources}
	PUBLIC FILE_SET HEADERS BASE_DIRS include FILES ${bedschem_headers}
)

target_compile_features(bedschem PUBLIC cxx_std_23)
bedschem_enable_warnings(bedschem)

