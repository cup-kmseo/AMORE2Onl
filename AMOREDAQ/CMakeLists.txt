set ( MODULE_NAME AMOREDAQ )
message ( STATUS "Configuring ${MODULE_NAME} ..." )

# Sources (normal .cc)
file ( GLOB MODULE_HEADERS ${MODULE_NAME}/*.hh )
file ( GLOB MODULE_SOURCES src/*.cc )

# --- Collect include dirs for dictionary ---
set ( DICT_I_FLAGS
    -I${LIBUSB_INCLUDE_DIRS}
    -I${CUPDAQ_INCLUDE_DIR}
    -I${CUPDATAMODEL_INCLUDE_DIR}
    -I${CMAKE_SOURCE_DIR}/AMORE
    -I${CMAKE_SOURCE_DIR}/AMORESystem
    -I${CMAKE_SOURCE_DIR}/AMOREAlgs
    -I${CMAKE_SOURCE_DIR}/AMOREHDF5
    -I${HDF5_INCLUDE_DIRS}
)

# Generate ROOT dictionary for the headers
ROOT_GENERATE_DICTIONARY ( G__${MODULE_NAME}
    ${MODULE_HEADERS}
    LINKDEF ${MODULE_NAME}/LinkDef.h
    OPTIONS ${DICT_I_FLAGS} -Wno-inconsistent-missing-override
)

# Build shared library (add dictionary source)
add_library ( ${MODULE_NAME} SHARED
    ${MODULE_SOURCES}
    G__${MODULE_NAME}.cxx
)

# Include paths
target_include_directories ( ${MODULE_NAME}
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    ${ROOT_INCLUDE_DIRS}
    ${HDF5_INCLUDE_DIRS}
)

# Link ROOT, submodule targets, and apply its compile defs
target_link_libraries ( ${MODULE_NAME}
    PUBLIC
    yaml-cpp
    cppzmq
    ${ROOT_LIBRARIES}
    ${HDF5_LIBRARIES}
    CUPDAQ::OnlConsts
    CUPDAQ::Notice
    CUPDAQ::OnlObjs
    CUPDataModel::RawObjs
    CUPDAQ::DAQUtils
    CUPDAQ::DAQSystem
    CUPDAQ::DAQConfig
    CUPDAQ::DAQTrigger
    CUPDAQ::DAQ
    CUPDataModel::HDF5Utils
    AMORE
    AMORESystem
    AMOREAlgs
    AMOREHDF5
)

target_compile_definitions ( ${MODULE_NAME}
    PUBLIC
    ${ROOT_DEFINITIONS}
)

# ---- Install ----
install ( TARGETS ${MODULE_NAME}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install ( DIRECTORY ${MODULE_NAME}/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${MODULE_NAME}
    FILES_MATCHING PATTERN "*.hh"
)
install ( FILES
    ${CMAKE_CURRENT_BINARY_DIR}/lib${MODULE_NAME}_rdict.pcm
    ${CMAKE_CURRENT_BINARY_DIR}/lib${MODULE_NAME}.rootmap
    DESTINATION ${CMAKE_INSTALL_LIBDIR}
    OPTIONAL
)

add_subdirectory ( test )