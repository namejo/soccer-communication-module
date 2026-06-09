cmake_minimum_required(VERSION 3.16)

project(rjscm VERSION 0.1.0 LANGUAGES C CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_library(rjscm STATIC src/rjscm.c)
target_include_directories(
    rjscm
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_compile_features(rjscm PUBLIC c_std_11)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(rjscm PRIVATE -Wall -Wextra -Wpedantic)
endif()

find_package(Threads REQUIRED)

enable_testing()

option(RJSCM_BUILD_EXAMPLES "Build rjscm example programs" ON)

add_executable(test_rjscm_c tests/test_rjscm.c)
target_link_libraries(test_rjscm_c PRIVATE rjscm Threads::Threads)
target_compile_features(test_rjscm_c PRIVATE c_std_11)
add_test(NAME rjscm_c COMMAND test_rjscm_c)

add_executable(test_rjscm_cpp tests/test_rjscm_cpp.cpp)
target_link_libraries(test_rjscm_cpp PRIVATE rjscm)
target_compile_features(test_rjscm_cpp PRIVATE cxx_std_14)
add_test(NAME rjscm_cpp COMMAND test_rjscm_cpp)

add_executable(
    test_firmware_sibcp_protocol
    tests/test_firmware_sibcp_protocol.cpp
    ../../firmware/RCj_comm_module/sibcp_protocol.cpp
)
target_include_directories(test_firmware_sibcp_protocol PRIVATE ../../firmware/RCj_comm_module)
target_compile_features(test_firmware_sibcp_protocol PRIVATE cxx_std_14)
add_test(NAME firmware_sibcp_protocol COMMAND test_firmware_sibcp_protocol)

if(RJSCM_BUILD_EXAMPLES)
    add_executable(see_ball_responder examples/see_ball_responder.c)
    target_link_libraries(see_ball_responder PRIVATE rjscm Threads::Threads)
    target_compile_features(see_ball_responder PRIVATE c_std_11)

    add_executable(see_ball_caller examples/see_ball_caller.cpp)
    target_link_libraries(see_ball_caller PRIVATE rjscm Threads::Threads)
    target_compile_features(see_ball_caller PRIVATE cxx_std_14)
endif()

install(
    TARGETS rjscm
    EXPORT rjscmTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(
    FILES include/rjscm.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(
    EXPORT rjscmTargets
    NAMESPACE rjscm::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/rjscm
)
configure_package_config_file(
    cmake/rjscmConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/rjscmConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/rjscm
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/rjscmConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(
    FILES
        ${CMAKE_CURRENT_BINARY_DIR}/rjscmConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/rjscmConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/rjscm
)
