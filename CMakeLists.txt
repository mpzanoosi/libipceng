# minimum required cmake
cmake_minimum_required(VERSION 3.9)

# project name
project(libipceng)

# versioning
set(VERSION_MAJOR 1)
set(VERSION_MINOR 0)
set(VERSION_PATCH 0)
set(VERSION_STRING ${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH})

# uncomment next line to enable testing
option(TEST "option to test ipc engine" OFF)

# directories for outputs
if(TEST)
	set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
else()
	set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endif()

set(CMAKE_LIBRARY_OUTPUT_DIRACTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# making libipceng
set(CMAKE_C_FLAGS "-rdynamic -fPIC -g -Wformat")
# adding source files
add_library(ipceng	SHARED
		"ipceng.c"
	)
# adding properties
set_target_properties(ipceng PROPERTIES 
	PUBLIC_HEADER "ipceng.h"
	VERSION ${VERSION_STRING}
	SOVERSION ${VERSION_MAJOR}
	)
# adding install method
INSTALL(TARGETS ipceng 
        LIBRARY DESTINATION lib/
        PUBLIC_HEADER DESTINATION include/
        LIBRARY PERMISSIONS WORLD_READ WORLD_WRITE WORLD_EXECUTE
	)
# actually making the library
target_link_libraries(ipceng -lrt -ldl -lm)

# unit testing
if(TEST)
	# test ipc engine
	add_executable(test_ipceng
			"test_ipceng.c"
		)
	target_link_libraries(test_ipceng ipceng)
endif()
