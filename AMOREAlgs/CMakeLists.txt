set(MODULE_NAME AMOREAlgs)
message(STATUS "Configuring ${MODULE_NAME} ...")

# Sources (normal .cc)
file(GLOB MODULE_HEADERS ${MODULE_NAME}/*.hh)
file(GLOB MODULE_SOURCES src/*.cc)

# --- Collect include dirs for dictionary ---
# IMPORTANT: Paths to CUPDAQ modules must now include the 'CUPDAQ/' prefix 
# because it is integrated as a submodule.
set(DICT_I_FLAGS
    -I${CMAKE_SOURCE_DIR}/CUPDAQ/OnlConsts
    -I${CMAKE_SOURCE_DIR}/CUPDAQ/DAQUtils
    -I${CMAKE_SOURCE_DIR}/CUPDAQ/DAQConfig
    -I${CMAKE_SOURCE_DIR}/AMORE
    -I${CMAKE_SOURCE_DIR}/AMORESystem
)

# Generate ROOT dictionary for only the two headers above
ROOT_GENERATE_DICTIONARY(G__${MODULE_NAME}
    ${MODULE_HEADERS}
    LINKDEF ${MODULE_NAME}/LinkDef.h
    OPTIONS ${DICT_I_FLAGS} -Wno-inconsistent-missing-override
)

# Build shared library (add dictionary source)
add_library(${MODULE_NAME} SHARED
    ${MODULE_SOURCES}
    G__${MODULE_NAME}.cxx
)

# Include paths
target_include_directories(${MODULE_NAME}
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    ${ROOT_INCLUDE_DIRS}
)

# Link ROOT, AMORESystem, and apply compile defs
target_link_libraries(${MODULE_NAME}
    PUBLIC
    ${ROOT_LIBRARIES}
    OnlConsts
    DAQUtils
    DAQConfig   
    AMORE
    AMORESystem
)

target_compile_definitions(${MODULE_NAME}
    PUBLIC
    ${ROOT_DEFINITIONS}
)

# ---- Install ----
install(TARGETS ${MODULE_NAME}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY ${MODULE_NAME}/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${MODULE_NAME}
    FILES_MATCHING PATTERN "*.hh"
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/lib${MODULE_NAME}_rdict.pcm
    ${CMAKE_CURRENT_BINARY_DIR}/lib${MODULE_NAME}.rootmap
    DESTINATION ${CMAKE_INSTALL_LIBDIR}
    OPTIONAL
)

add_subdirectory(test)