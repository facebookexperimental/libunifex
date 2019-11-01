
file(GLOB test-sources "*_test.cpp")
foreach(file-path ${test-sources})
    string( REPLACE ".cpp" "" file-path-without-ext ${file-path} )
    get_filename_component(file-name ${file-path-without-ext} NAME)
    add_executable( ${file-name} ${file-path})
    target_link_libraries( ${file-name} PUBLIC unifex)
    add_test(NAME "test-${file-name}" COMMAND ${file-name})

endforeach()
