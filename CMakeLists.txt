cmake_minimum_required(VERSION 3.30)

project(bedschem LANGUAGES CXX)

include(cmake/warnings.cmake)
include(cmake/CPM.cmake)

CPMAddPackage(
	NAME libdeflate
	GITHUB_REPOSITORY ebiggers/libdeflate
	GIT_TAG v1.26
	OPTIONS
		"LIBDEFLATE_BUILD_SHARED_LIB OFF"
		"LIBDEFLATE_BUILD_GZIP OFF"
		"LIBDEFLATE_BUILD_TESTS OFF"
		"LIBDEFLATE_COMPRESSION_SUPPORT OFF"
		"LIBDEFLATE_INSTALL OFF"
)

file(GLOB_RECURSE bedschem_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
file(GLOB_RECURSE bedschem_headers CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h")

add_library(bedschem STATIC)
add_library(bedschem::bedschem ALIAS bedschem)

target_sources(bedschem
	PRIVATE ${bedschem_sources}
	PUBLIC FILE_SET HEADERS BASE_DIRS include FILES ${bedschem_headers}
)

target_include_directories(bedschem PRIVATE src)
target_link_libraries(bedschem PRIVATE libdeflate::libdeflate_static)
target_compile_features(bedschem PUBLIC cxx_std_23)
bedschem_enable_warnings(bedschem)

set_source_files_properties(src/java/block_data.cpp PROPERTIES OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/java/block_data.nbt")
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
	set_source_files_properties(src/java/block_data.cpp PROPERTIES COMPILE_OPTIONS "-Wno-c23-extensions")
endif()

