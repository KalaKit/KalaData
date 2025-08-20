cmake_minimum_required(VERSION 3.29.2)

set(KALADATA_VERSION "KalaData 0.2 Alpha")
set(KALADATA_VERSION_NUMBER 0.2.0.0)

project("KalaData" VERSION ${KALADATA_VERSION_NUMBER} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Platform Detection
if (WIN32)
    message(STATUS "[KALADATA] Platform = Windows")
elseif(UNIX)
    find_package(X11 REQUIRED)
    message(STATUS "[KALADATA] Platform = Linux")
else()
    message(FATAL_ERROR "[KALADATA] Unsupported platform. Must be Windows or Linux.")
endif()

# Build Type Detection
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(IS_DEBUG TRUE)
elseif(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    set(IS_RELEASE TRUE)
else()
    message(FATAL_ERROR "Unknown CMAKE_BUILD_TYPE: '${CMAKE_BUILD_TYPE}'! Must be Debug, Release, or RelWithDebInfo.")
endif()

# Runtime lib handling for MSVC
if (MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Directory Setup
set(INCLUDE_DIR "${CMAKE_SOURCE_DIR}/include")
set(EXT_SHARED_DIR "${CMAKE_SOURCE_DIR}/_external_shared")

# Source Files
file(GLOB_RECURSE SOURCE_FILES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/*/*.cpp"
)

# Executable
add_executable(KalaData ${SOURCE_FILES})

# Enable exception handling for MSVC
if (MSVC)
    target_compile_options(KalaData PRIVATE /EHsc)
endif()

set_target_properties(KalaData PROPERTIES OUTPUT_NAME "KalaData")
target_compile_features(KalaData PRIVATE cxx_std_20)

# Includes
file(GLOB_RECURSE HEADERS configure_depends
	"${CMAKE_SOURCE_DIR}/include/*.hpp"
)
target_sources(KalaData PRIVATE ${HEADERS})
target_include_directories(KalaData PRIVATE 
	${INCLUDE_DIR}
	${EXT_SHARED_DIR}
)

# Preprocessor Defines
if (WIN32)
    target_compile_definitions(KalaData PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

# Link libraries
if (UNIX)
    target_link_libraries(KalaData PRIVATE ${X11_LIBRARIES})
endif()

# Set Windows Details tab data
if (WIN32)
	if (IS_RELEASE)
		set(FILEFLAGS "0x00")
	else()
		set(FILEFLAGS "0x01")
	endif()
	
	set(FILETYPE 0x1)    # 0x1 - executable, 0x2 - dynamic-link library
	set(FILESUBTYPE 0x0) # 0x0 = normal DLL, only change if you're making drivers/fonts

	# Public fields displayed in Windows Explorer
	set(FILE_DESCRIPTION  "Data packer/unpacker")
	set(FILE_VERSION
		"${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}.${PROJECT_VERSION_TWEAK}")
	set(PRODUCT_NAME      "KalaData")
	set(PRODUCT_VERSION
		"${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}.${PROJECT_VERSION_TWEAK}")
	set(COMPANY_NAME      "Lost Empire Entertainment")
	set(LEGAL_COPYRIGHT   "(C) 2025 Lost Empire Entertainment")
	set(LEGAL_TRADEMARKS  "Trademark of Lost Empire Entertainment")
	set(ORIGINAL_FILENAME "${PRODUCT_NAME}")

	# Private/optional fields
	set(INTERNAL_NAME  "${PRODUCT_NAME}")
	set(COMMENTS       "${PRODUCT_NAME}")
	set(PRIVATE_BUILD  "${PRODUCT_NAME}")
	set(SPECIAL_BUILD  "${PRODUCT_NAME}")

	# Generate version.rc
	configure_file(
		${CMAKE_SOURCE_DIR}/version.rc.in
		${CMAKE_BINARY_DIR}/version.rc
		@ONLY
	)

    target_sources(KalaData PRIVATE "${CMAKE_BINARY_DIR}/version.rc")
endif()

# Expose version string
add_compile_definitions(
	KALADATA_VERSION="${KALADATA_VERSION}"
)

# Installation
set(CMAKE_INSTALL_BINDIR bin)
install(TARGETS KalaData DESTINATION ${CMAKE_INSTALL_BINDIR})

# Copy docs
set(COPY_TARGETS README.md LICENSE.md CHANGES.txt)
foreach(FILE ${COPY_TARGETS})
add_custom_command(TARGET KalaData POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E remove "$<TARGET_FILE_DIR:KalaData>/${FILE}"
    COMMAND ${CMAKE_COMMAND} -E copy
		"${CMAKE_SOURCE_DIR}/${FILE}"
		"$<TARGET_FILE_DIR:KalaData>/${FILE}"
)
endforeach()

# Package
include(CPack)
