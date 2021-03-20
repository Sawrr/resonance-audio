# Copyright 2018 Google Inc. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS-IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

if (WIN32)
    set(STATIC_LIB_EXT ".lib")
    if (CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(THIRD_PARTY_INSTALL_DIR "install64")
        set(EMBREE_BUILD_DIR "build64/Release")
    else ()
        set(THIRD_PARTY_INSTALL_DIR "install32")
        set(EMBREE_BUILD_DIR "build32/Release")
    endif ()
else ()
    set(STATIC_LIB_EXT ".a")
    set(THIRD_PARTY_INSTALL_DIR "install")
    set(EMBREE_BUILD_DIR "build")
endif ()

set(LIBEMBREE_DIR "${PROJECT_SOURCE_DIR}/third_party/embree" CACHE PATH "Path to libEmbree installation")
set(LIBEMBREE_INCLUDE_DIR "${LIBEMBREE_DIR}/include")

# Create STATIC IMPORTED library targets for static 3P dependencies.

# Embree's CMake config doesn't export all static libraries in its install target.
# (see https://github.com/embree/embree/issues/130)
# To statically link embree into the Godot plugin, all static libraries from
# ../third_party/embree/build are manually added to the "${TARGET}" target.
set(EMBREE_LIB_DIR "${LIBEMBREE_DIR}/${EMBREE_BUILD_DIR}")

if (NOT (IOS_DETECTED OR ANDROID))
    file(GLOB EXTERN_LIBS ABSOLUTE "${EMBREE_LIB_DIR}/*${STATIC_LIB_EXT}")
endif ()

foreach (EXTERN_LIB ${EXTERN_LIBS})
    get_filename_component(EXTERN_LIB_FILENAME ${EXTERN_LIB} NAME)
    string(REPLACE "." "_" EXTERN_LIB_TARGETNAME ${EXTERN_LIB_FILENAME})
    string(TOUPPER ${EXTERN_LIB_TARGETNAME} EXTERN_LIB_TARGETNAME)
    add_library(${EXTERN_LIB_TARGETNAME} STATIC IMPORTED)
    set_target_properties(${EXTERN_LIB_TARGETNAME} PROPERTIES
            IMPORTED_LOCATION "${EXTERN_LIB}"
            )
endforeach ()

function(TargetLinkExternal3PStaticLibs TARGET)
    set(WHOLE_ARCHIVE_FLAGS "")
    foreach (EXTERN_LIB ${EXTERN_LIBS})
        get_filename_component(EXTERN_LIB_FILENAME ${EXTERN_LIB} NAME)
        string(REPLACE "." "_" EXTERN_LIB_TARGETNAME ${EXTERN_LIB_FILENAME})
        string(TOUPPER ${EXTERN_LIB_TARGETNAME} EXTERN_LIB_TARGETNAME)
        message(STATUS "Statically linking to: " ${EXTERN_LIB})
        if (MSVC)
            target_link_libraries("${TARGET}" ${EXTERN_LIB_TARGETNAME})
            set(WHOLE_ARCHIVE_FLAGS "${WHOLE_ARCHIVE_FLAGS} /WHOLEARCHIVE:\"${EXTERN_LIB_FILENAME}\"")
        elseif (APPLE)
            target_link_libraries("${TARGET}" ${EXTERN_LIB_TARGETNAME})
        else ()
            SET(WHOLE_ARCHIVE_BRACKET -Wl,--whole-archive ${EXTERN_LIB_TARGETNAME} -Wl,--no-whole-archive)
            target_link_libraries("${TARGET}" ${WHOLE_ARCHIVE_BRACKET})
        endif ()
    endforeach ()

    if (APPLE)
        set_target_properties("${TARGET}" PROPERTIES
                LINK_FLAGS "-Wl,-all_load"
                )
        if (NOT IOS_DETECTED)
            set_target_properties("${TARGET}" PROPERTIES
                    PREFIX "" SUFFIX ".bundle"
                    )
        endif (NOT IOS_DETECTED)
    elseif (MSVC)
        set_target_properties("${TARGET}" PROPERTIES
                LINK_FLAGS "/MANIFEST:NO ${WHOLE_ARCHIVE_FLAGS}"
                )
    endif ()
endfunction()

set(RA_GEO_ACOUSTICS_SOURCES
        ${RA_SOURCE_DIR}/geometrical_acoustics/acoustic_listener.h
        ${RA_SOURCE_DIR}/geometrical_acoustics/acoustic_ray.cc
        ${RA_SOURCE_DIR}/geometrical_acoustics/acoustic_ray.h
        ${RA_SOURCE_DIR}/geometrical_acoustics/acoustic_source.h
        ${RA_SOURCE_DIR}/geometrical_acoustics/collection_kernel.cc
        ${RA_SOURCE_DIR}/geometrical_acoustics/collection_kernel.h
        ${RA_SOURCE_DIR}/geometrical_acoustics/estimating_rt60.cc
        ${RA_SOURCE_DIR}/geometrical_acoustics/estimating_rt60.h
        ${RA_SOURCE_DIR}/geometrical_acoustics/impulse_response_computer.cc
        ${RA_SOURCE_DIR}/geometrical_acoustics/impulse_response_computer.h
        ${RA_SOURCE_DIR}/geometrical_acoustics/mesh.h
        ${RA_SOURCE_DIR}/geometrical_acoustics/parallel_for.cc
        ${RA_SOURCE_DIR}/geometrical_acoustics/parallel_for.h
        ${RA_SOURCE_DIR}/geometrical_acoustics/path.h
        ${RA_SOURCE_DIR}/geometrical_acoustics/path_tracer.cc
        ${RA_SOURCE_DIR}/geometrical_acoustics/path_tracer.h
        ${RA_SOURCE_DIR}/geometrical_acoustics/proxy_room_estimator.cc
        ${RA_SOURCE_DIR}/geometrical_acoustics/proxy_room_estimator.h
        ${RA_SOURCE_DIR}/geometrical_acoustics/reflection_kernel.cc
        ${RA_SOURCE_DIR}/geometrical_acoustics/reflection_kernel.h
        ${RA_SOURCE_DIR}/geometrical_acoustics/sampling.h
        ${RA_SOURCE_DIR}/geometrical_acoustics/scene_manager.cc
        ${RA_SOURCE_DIR}/geometrical_acoustics/scene_manager.h
        ${RA_SOURCE_DIR}/geometrical_acoustics/sphere.cc
        ${RA_SOURCE_DIR}/geometrical_acoustics/sphere.h
        ${RA_SOURCE_DIR}/utils/task_thread_pool.cc
        ${RA_SOURCE_DIR}/utils/task_thread_pool.h
        )

add_library(GeometricalAcousticsObj OBJECT ${RA_GEO_ACOUSTICS_SOURCES})
target_include_directories(GeometricalAcousticsObj PRIVATE ${RA_SOURCE_DIR})
target_include_directories(GeometricalAcousticsObj PRIVATE ${LIBEMBREE_INCLUDE_DIR})
target_compile_options(GeometricalAcousticsObj PRIVATE "-DEMBREE_STATIC_LIB")

set(RA_GODOT_SOURCE_DIR "${PROJECT_SOURCE_DIR}/platforms/godot")
set(RA_GODOT_SOURCES
        $<TARGET_OBJECTS:ResonanceAudioObj>
        $<TARGET_OBJECTS:SadieHrtfsObj>
        $<TARGET_OBJECTS:PffftObj>
        $<TARGET_OBJECTS:GeometricalAcousticsObj>
        ${RA_GODOT_SOURCE_DIR}/godot_reverb_computer.h
        ${RA_GODOT_SOURCE_DIR}/godot_reverb_computer.cc
        )

add_library(audiopluginresonanceaudio STATIC ${RA_GODOT_SOURCES})
set(PFFFT_INCLUDE_DIR ${PFFFT_DIR})
target_include_directories(audiopluginresonanceaudio PRIVATE ${RA_SOURCE_DIR})
target_include_directories(audiopluginresonanceaudio PRIVATE ${LIBEMBREE_INCLUDE_DIR})
target_include_directories(audiopluginresonanceaudio PRIVATE ${EIGEN3_INCLUDE_DIR}/)
target_include_directories(audiopluginresonanceaudio PRIVATE ${PFFFT_INCLUDE_DIR}/)

TargetLinkExternal3PStaticLibs(audiopluginresonanceaudio)
if (MSVC)
    # Embree pulls in WinSock32
    target_link_libraries(audiopluginresonanceaudio wsock32 ws2_32)
endif (MSVC)

set(GODOT_INSTALL_DIR "${INSTALL_DIR}/godot/install/")
set(INSTALL_BIN_DIR "")
if (MSVC)
    if (CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(INSTALL_BIN_DIR "x86_64")
    else ()
        set(INSTALL_BIN_DIR "x86")
    endif ()
elseif (ANDROID)
    if ("${ANDROID_ABI}" MATCHES "armeabi-v7a")
        set(INSTALL_BIN_DIR "Android/libs/armeabi-v7a")
    elseif ("${ANDROID_ABI}" MATCHES "x86")
        set(INSTALL_BIN_DIR "Android/libs/x86")
    elseif ("${ANDROID_ABI}" MATCHES "arm64-v8a")
        set(INSTALL_BIN_DIR "Android/libs/arm64-v8a")
    elseif ("${ANDROID_ABI}" MATCHES "x86_64")
        set(INSTALL_BIN_DIR "Android/libs/x86_64")
    endif ()
elseif (APPLE)
    if (IOS_DETECTED)
        set(INSTALL_BIN_DIR "iOS")
    else ()
        set(INSTALL_BIN_DIR "x86_64")
    endif ()
elseif (UNIX)
    set(INSTALL_BIN_DIR "x86_64")
endif ()

install(TARGETS audiopluginresonanceaudio
        LIBRARY DESTINATION "${GODOT_INSTALL_DIR}/${INSTALL_BIN_DIR}/"
        ARCHIVE DESTINATION "${GODOT_INSTALL_DIR}/${INSTALL_BIN_DIR}/"
        RUNTIME DESTINATION "${GODOT_INSTALL_DIR}/${INSTALL_BIN_DIR}/")
