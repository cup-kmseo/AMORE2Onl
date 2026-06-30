set ( TEST_SOURCES
    amoretcbdaq.cc
    amoredaq.cc
)

foreach ( src ${TEST_SOURCES} )
    get_filename_component ( test_name ${src} NAME_WE )

    add_executable ( ${test_name} ${src} )

    target_include_directories ( ${test_name}
        PRIVATE
            $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/AMOREDAQ>
    )

    target_link_libraries ( ${test_name}
        PRIVATE
            ${ROOT_LIBRARIES}
            PkgConfig::LIBUSB
            CUPDAQ::DAQUtils
            CUPDAQ::DAQConfig
            CUPDAQ::DAQSystem
            CUPDAQ::OnlObjs
            CUPDataModel::RawObjs
            CUPDAQ::Notice
            AMOREDAQ
    )

    install ( TARGETS ${test_name}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endforeach ()

# --- RValueTrigger offline replay test (requires HDF5) ---
if ( CUPDAQ_ENABLE_HDF5 )
    add_executable ( rv_trigger_test rv_trigger_test.cc )

    target_link_libraries ( rv_trigger_test
        PRIVATE
            ${ROOT_LIBRARIES}
            ${HDF5_LIBRARIES}
            AMOREDAQ
    )

    install ( TARGETS rv_trigger_test
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif ()
