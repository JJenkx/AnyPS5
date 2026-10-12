add_library(libSceCesCs-module SHARED EXCLUDE_FROM_ALL ../libSceCesCs/Export.cpp)
target_include_directories(libSceCesCs-module PRIVATE ${LIBS_INCLUDE_DIR})
configure_windows_unwind(libSceCesCs-module)

if(BUILD_TESTING)
    add_test_executable(guest_ces_module_tests ${LIBS_INCLUDE_DIR}/tests/GuestCes.cpp)
    target_include_directories(guest_ces_module_tests PRIVATE ${LIBS_INCLUDE_DIR})
    target_link_libraries(guest_ces_module_tests PRIVATE libSceCesCs-module)
    add_test(NAME guest_ces_module COMMAND guest_ces_module_tests)
    if(WIN32)
        set_tests_properties(guest_ces_module PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:libSceCesCs-module>")
    endif()
endif()
