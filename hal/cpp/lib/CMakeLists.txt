# Copyright (c) Prophesee S.A.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
# Unless required by applicable law or agreed to in writing, software distributed under the License is distributed
# on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and limitations under the License.

#### Create obj library with software info
add_library(metavision_hal_info_obj OBJECT)
# The following line is needed becase when linking a shared library to an object one,
# the object library needs to be compiled with -fPIC
# cf https://stackoverflow.com/questions/50600708/combining-cmake-object-libraries-with-shared-libraries
set_target_properties(metavision_hal_info_obj
    PROPERTIES
        POSITION_INDEPENDENT_CODE ON
)

target_include_directories(metavision_hal_info_obj
    PUBLIC
        ${metavision_hal_include_dir}
    PRIVATE
        ${GENERATE_FILES_DIRECTORY}/include # For software_info.cpp
)
target_link_libraries(metavision_hal_info_obj
    PUBLIC
        MetavisionSDK::base
)
add_dependencies(metavision_hal_info_obj generate_metavision_hal_version_header)

#### metavision_hal
add_library(metavision_hal SHARED)
target_sources(metavision_hal PRIVATE $<TARGET_OBJECTS:metavision_hal_info_obj>)
target_include_directories(metavision_hal
    PUBLIC
        $<BUILD_INTERFACE:${metavision_hal_include_dir}>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${install_paths_infos_dir}
)

target_link_libraries(metavision_hal
    PUBLIC
        MetavisionSDK::base
        Threads::Threads
    PRIVATE
        metavision_hal_info_obj
)

include(GenerateExportHeader)
if (WIN32 OR CYGWIN)
    set (_custom_export_content "
        #ifndef METAVISION_HAL_EXTERN_EXPORT 
        #  define METAVISION_HAL_EXTERN_EXPORT __declspec(dllexport)
        #endif"
    )
else ()
    set (_custom_export_content "
        #ifndef METAVISION_HAL_EXTERN_EXPORT 
        #  define METAVISION_HAL_EXTERN_EXPORT METAVISION_HAL_EXPORT
        #endif"
    )
endif ()

GENERATE_EXPORT_HEADER(
    metavision_hal
    EXPORT_MACRO_NAME METAVISION_HAL_EXPORT
    EXPORT_FILE_NAME ${GENERATE_FILES_DIRECTORY}/include/metavision/hal/metavision_hal_export.h
    CUSTOM_CONTENT_FROM_VARIABLE _custom_export_content
)
target_include_directories(metavision_hal
    PUBLIC
        "$<BUILD_INTERFACE:${GENERATE_FILES_DIRECTORY}/include>"
)

if (ANDROID)
    # Fixme in TEAM-9084
    # This is a temporary hack to force gradle to package libusb in the APK so that 
    # plugins can link correctly at run time.
    # A better solution is to make sure the plugins on Android do not link with libusb
    # since they don't need it (offline support only)
    target_link_libraries(metavision_hal
        PUBLIC
            libusb-1.0
    )
endif (ANDROID)

#### metavision_hal_discovery
add_library(metavision_hal_discovery SHARED)
target_include_directories(metavision_hal_discovery
    PUBLIC
        $<BUILD_INTERFACE:${metavision_hal_include_dir}>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(metavision_hal_discovery
    PUBLIC
        metavision_hal
    PRIVATE
        metavision_hal_info_obj
        ${CMAKE_DL_LIBS}
)

set(METAVISION_HAL_EXPORT_FILE_NAME MetavisionHALTargets.cmake)
set(metavision_hal_config_files_output_dir "${GENERATE_FILES_DIRECTORY}/share/cmake/MetavisionHALCMakePackagesFilesDir")
# Set VERSION, SOVERSION and EXPORT_NAME for targets metavision_hal and metavision_hal_discovery
foreach(targ metavision_hal metavision_hal_discovery)

    # We export metavision_hal metavision_hal_discovery with name
    # HAL and HAL_discovery, so that users may refer to them
    # with Metavision::Hal and Metavision::Hal_discovery (because we export
    # the targets under the namespace Metavision - see below)
    string(REPLACE "metavision_hal" "HAL" exp_name "${targ}")

    add_library(Metavision::${exp_name} ALIAS ${targ})

    set_target_properties(${targ}
        PROPERTIES
            VERSION ${PROJECT_VERSION}
            SOVERSION ${PROJECT_VERSION_MAJOR}
            EXPORT_NAME ${exp_name}
    )

    # Install target
    install(TARGETS ${targ}
            EXPORT MetavisionHALTargets
            RUNTIME
                DESTINATION ${RUNTIME_INSTALL_DEST}
                COMPONENT metavision-hal
            ARCHIVE
                DESTINATION ${ARCHIVE_INSTALL_DEST}
                COMPONENT metavision-hal
            LIBRARY
                DESTINATION ${LIBRARY_INSTALL_DEST}
                COMPONENT metavision-hal
                NAMELINK_SKIP
    )
    install(TARGETS ${targ}
            EXPORT MetavisionHALTargets
            LIBRARY
                DESTINATION ${LIBRARY_INSTALL_DEST}
                COMPONENT metavision-hal-dev
                NAMELINK_ONLY
    )
endforeach(targ)

export(EXPORT MetavisionHALTargets
       FILE ${metavision_hal_config_files_output_dir}/${METAVISION_HAL_EXPORT_FILE_NAME}
       NAMESPACE Metavision::
)

# Install public headers
install(DIRECTORY ${metavision_hal_include_dir}/metavision
        DESTINATION include
        COMPONENT metavision-hal-dev
)
install(FILES ${GENERATE_FILES_DIRECTORY}/include/metavision/hal/metavision_hal_export.h
        DESTINATION include/metavision/hal
        COMPONENT metavision-hal-dev
)

# Install exported MetavisionHALTargets (containing targets metavision_hal and metavision_hal_discovery
install(EXPORT MetavisionHALTargets
        FILE ${METAVISION_HAL_EXPORT_FILE_NAME}
        NAMESPACE Metavision::
        DESTINATION share/cmake/MetavisionHAL
        COMPONENT metavision-hal-dev
)

# Create and install configuration files
include(CMakePackageConfigHelpers)
set(metavision_hal_config_file "${metavision_hal_config_files_output_dir}/MetavisionHALConfig.cmake")
configure_package_config_file(
    "${PROJECT_SOURCE_DIR}/hal/cmake/MetavisionHALConfig.cmake.in"
    "${metavision_hal_config_file}"
    INSTALL_DESTINATION share/cmake/MetavisionHAL
)
set(metavision_hal_config_version_file "${metavision_hal_config_files_output_dir}/MetavisionHALConfigVersion.cmake")
write_basic_package_version_file(
    "${metavision_hal_config_version_file}"
    COMPATIBILITY ExactVersion
)

install(FILES ${metavision_hal_config_file} ${metavision_hal_config_version_file}
        DESTINATION share/cmake/MetavisionHAL
        COMPONENT metavision-hal-dev
)