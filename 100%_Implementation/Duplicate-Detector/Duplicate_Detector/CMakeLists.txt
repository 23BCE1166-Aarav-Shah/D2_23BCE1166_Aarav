cmake_minimum_required(VERSION 3.16)

project(DuplicateOfflineApp VERSION 1.0.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(DUPLICATE_APP_BUNDLE_RUNTIME_DEPS "Copy runtime shared libraries into the install tree." ON)

find_package(OpenCV REQUIRED COMPONENTS core imgcodecs imgproc videoio)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil libswresample)
pkg_check_modules(CHROMAPRINT REQUIRED IMPORTED_TARGET libchromaprint)
find_package(Threads REQUIRED)

add_library(duplicate_backend_core STATIC
    duplicate_detector.cpp
    file_manager.cpp
    file_scanner.cpp
    hash_engine.cpp
)

add_library(duplicate_finder STATIC
    src/config.cpp
    src/database_cache.cpp
    src/duplicate_engine.cpp
    src/duplicate_detector.cpp
    src/file_manager.cpp
    src/file_scanner.cpp
    src/hash_engine.cpp
    src/job_manager.cpp
    src/logger.cpp
    src/preview_engine.cpp
    src/watcher_service.cpp
)

target_include_directories(duplicate_backend_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_include_directories(duplicate_finder
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(duplicate_finder
    PUBLIC
        ${OpenCV_LIBS}
        PkgConfig::FFMPEG
        PkgConfig::CHROMAPRINT
        dl
)

add_executable(duplicate_backend_service
    backend_service.cpp
    backend_service_main.cpp
)

target_include_directories(duplicate_backend_service
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(duplicate_backend_service
    PRIVATE
        duplicate_finder
        Threads::Threads
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(duplicate_backend_core PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(duplicate_finder PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(duplicate_backend_service PRIVATE -Wall -Wextra -Wpedantic)
    target_link_options(duplicate_backend_service PRIVATE -Wl,--no-as-needed)
endif()

install(TARGETS duplicate_backend_core duplicate_finder duplicate_backend_service
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(FILES
    include/duplicate_finder/config.hpp
    include/duplicate_finder/database_cache.hpp
    include/duplicate_finder/duplicate_detector.hpp
    include/duplicate_finder/duplicate_engine.hpp
    include/duplicate_finder/file_manager.hpp
    include/duplicate_finder/file_scanner.hpp
    include/duplicate_finder/hash_engine.hpp
    include/duplicate_finder/job_manager.hpp
    include/duplicate_finder/library.hpp
    include/duplicate_finder/logger.hpp
    include/duplicate_finder/preview_engine.hpp
    include/duplicate_finder/types.hpp
    include/duplicate_finder/watcher_service.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/duplicate-offline-app
)

if(DUPLICATE_APP_BUNDLE_RUNTIME_DEPS)
    set(BUNDLE_EXE "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/duplicate_backend_service")
    set(BUNDLE_OPEN_CV_DIRS "${OpenCV_LIBRARY_DIRS}")
    set(BUNDLE_PKG_DIRS "${FFMPEG_LIBRARY_DIRS};${CHROMAPRINT_LIBRARY_DIRS}")
    set(BUNDLE_INSTALL_LIBDIR "${CMAKE_INSTALL_LIBDIR}")
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/BundleRuntimeDeps.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/BundleRuntimeDeps.cmake
        @ONLY
    )
    install(SCRIPT ${CMAKE_CURRENT_BINARY_DIR}/BundleRuntimeDeps.cmake)
endif()

if(BUILD_TESTING)
    add_executable(duplicate_scan_integration_runner
        tests/integration/scan_pipeline_runner.cpp
    )

    target_include_directories(duplicate_scan_integration_runner
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    target_link_libraries(duplicate_scan_integration_runner
        PRIVATE
            duplicate_finder
            Threads::Threads
    )

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(duplicate_scan_integration_runner PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/googletest/CMakeLists.txt")
        add_subdirectory(third_party/googletest EXCLUDE_FROM_ALL)
    else()
        find_package(GTest QUIET)
    endif()

    if(TARGET GTest::gtest)
        add_executable(duplicate_finder_tests
            tests/hash_engine_test.cpp
            tests/duplicate_engine_test.cpp
            tests/test_main.cpp
        )

        target_include_directories(duplicate_finder_tests
            PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}
                ${CMAKE_CURRENT_SOURCE_DIR}/include
        )

        target_link_libraries(duplicate_finder_tests
            PRIVATE
                duplicate_finder
                GTest::gtest
                Threads::Threads
        )

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(duplicate_finder_tests PRIVATE -Wall -Wextra -Wpedantic)
        endif()

        include(GoogleTest)
        gtest_discover_tests(duplicate_finder_tests)
    else()
        message(WARNING "GoogleTest not found. duplicate_finder_tests target will not be generated.")
    endif()
endif()
