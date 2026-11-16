# if the library is compiled by vcpkg during the port build (i.e. during the package installation),
# then we don't need to build unit tests
if(IS_VCPKG_PORT_BUILD)
    return()
endif()

# no unit tests for ios
if(IOS)
    return()
endif()

set(test_srcs)
myci_add_source_files(test_srcs
    DIRECTORY
        ${CMAKE_CURRENT_LIST_DIR}/../../tests/net
    RECURSIVE
)

myci_declare_application(${PROJECT_NAME}-tests
    SOURCES
        ${test_srcs}
    DEPENDENCIES
        setka
        opros
        nitki
        fsif
        utki
)

# The tests perform live network DNS lookups and socket communication, so do not add them to the
# general test target.
# myci_declare_test(
#     RUN_TARGET
#         run-${PROJECT_NAME}-tests
# )
