cmake_minimum_required(VERSION 3.16)

project(delearner VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(IOS)
    enable_language(OBJCXX)
endif()

find_package(Felgo QUIET)


# Project identifier and version
# More information: https://felgo.com/doc/felgo-publishing/#project-configuration
set(PRODUCT_IDENTIFIER "com.yourcompany.wizardEVAP.delearner")
set(PRODUCT_VERSION_NAME "1.0.0")
set(PRODUCT_VERSION_CODE 1)

# Set either "test" or "publish" stage:
set(PRODUCT_STAGE "test")

# Optionally set a license key that is used instead of the license key from
# main.qml file (App::licenseKey for your app or GameWindow::licenseKey for your game)
# Only used for local builds and Felgo Cloud Builds (https://felgo.com/cloud-builds)
# Not used if using Felgo Developer App
set(PRODUCT_LICENSE_KEY "")

# set(FELGO_PLUGINS
#     admob
# )

# Find all QML/JS files for the QML compiler:
file(GLOB_RECURSE QmlFiles CONFIGURE_DEPENDS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} qml/*.qml qml/*.js)

# Find all non-QML/JS files in the qml and assets folder to add as resources:
file(GLOB_RECURSE AssetsFiles CONFIGURE_DEPENDS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} assets/* qml/*)
list(REMOVE_ITEM AssetsFiles ${QmlFiles})
list(APPEND AssetsFiles assets/icons/sort_words_icon.svg)
list(REMOVE_DUPLICATES AssetsFiles)

find_package(Qt6 REQUIRED COMPONENTS Core Network Sql)

set(DELEARNER_BUILD_TESTS_DEFAULT ON)
if(IOS)
    set(DELEARNER_BUILD_TESTS_DEFAULT OFF)
endif()
option(DELEARNER_BUILD_TESTS "Build the frontend test suite." ${DELEARNER_BUILD_TESTS_DEFAULT})
if(DELEARNER_BUILD_TESTS)
    find_package(Qt6 REQUIRED COMPONENTS Test)
endif()

option(DELEARNER_BUILD_APP "Build the Felgo application target." ON)
if(DELEARNER_BUILD_APP AND NOT Felgo_FOUND)
    message(WARNING "Felgo was not found; disabling the application target and building tests/core only.")
    set(DELEARNER_BUILD_APP OFF)
endif()

set(DELEARNER_CORE_SOURCES
    src/Auth/DLAuthSession.cpp
    src/Auth/DLAuthSession.h
    src/Auth/DLAuthSessionStore.cpp
    src/Auth/DLAuthSessionStore.h
    src/Auth/DLClientAuthService.cpp
    src/Auth/DLClientAuthService.h
    src/Logging/DLLogging.cpp
    src/Logging/DLLogging.h
    src/Managers/DLDatabaseManager.cpp
    src/Managers/DLDatabaseManager.h
    src/Repositories/DLGroupRepository.cpp
    src/Repositories/DLGroupRepository.h
    src/Repositories/DLQuizRepository.cpp
    src/Repositories/DLQuizRepository.h
    src/Repositories/DLReviewStatsRepository.cpp
    src/Repositories/DLReviewStatsRepository.h
    src/Repositories/DLWordRepository.cpp
    src/Repositories/DLWordRepository.h
    src/Services/DLDatabaseMaintenanceService.cpp
    src/Services/DLDatabaseMaintenanceService.h
    src/Services/DLGroupService.cpp
    src/Services/DLGroupService.h
    src/Services/DLQuizService.cpp
    src/Services/DLQuizService.h
    src/Services/DLQuizSession.cpp
    src/Services/DLQuizSession.h
    src/Services/DLWordService.cpp
    src/Services/DLWordService.h
    src/Sync/DLDeviceIdentityStore.cpp
    src/Sync/DLDeviceIdentityStore.h
    src/Sync/DLBootstrapStateRestorer.cpp
    src/Sync/DLBootstrapStateRestorer.h
    src/Sync/DLRemoteChangeReconciler.cpp
    src/Sync/DLRemoteChangeReconciler.h
    src/Sync/DLSyncCoordinator.cpp
    src/Sync/DLSyncCoordinator.h
    src/Sync/DLSyncRequestBuilder.cpp
    src/Sync/DLSyncRequestBuilder.h
    src/Sync/DLSyncEventSerializer.cpp
    src/Sync/DLSyncEventSerializer.h
    src/Models/DLModelMappers.cpp
    src/Models/DLModelMappers.h
    src/Models/DLQuizQuestion.h
    src/Models/DLQuizResult.h
    src/Models/DLWord.h
    src/Models/DLWordForms.h
    src/Models/DLWordGroup.h
    src/Models/DLWordReviewStats.h
)

qt_add_library(delearner_core STATIC
    ${DELEARNER_CORE_SOURCES}
)

target_include_directories(delearner_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Auth
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Logging
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Managers
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Models
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Repositories
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Services
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Sync
)
target_link_libraries(delearner_core PUBLIC Qt6::Core Qt6::Network Qt6::Sql)

if(DELEARNER_BUILD_APP)
    qt_add_executable(delearner
        main.cpp
        src/Managers/DLAppStateManager.cpp
        src/Managers/DLAppStateManager.h
        src/Services/DLDatabaseExportService.cpp
        src/Services/DLDatabaseExportService.h
        ${QmlFiles}
        ${AssetsFiles}
    )

    if(IOS)
        target_sources(delearner PRIVATE src/Services/DLDatabaseExportService_ios.mm)
    endif()

    felgo_configure_executable(delearner)

# Deploy resources to build folder/package directly
# comment for publishing
    deploy_resources("${QmlFiles};${AssetsFiles}")

# Add QML files and resources to QML module to included them via QRC automatically:
    qt_add_qml_module(delearner
        URI delearner
        VERSION 1.0
# uncomment for publishing:
        QML_FILES ${QmlFiles}
        RESOURCES ${AssetsFiles}
        NO_RESOURCE_TARGET_PATH
    )

# NOTE: for PUBLISHING, perform the following steps:
# 1. comment the deploy_resources line above, to avoid shipping your qml files with the application (instead they get compiled to the app binary)
# 2. uncomment the QML_FILES and RESOURCES above; this compiles your qml files and js files to the app binary and protects your source code
# 3. change the setMainQmlFile() call in main.cpp to the one starting with "qrc:/" - this loads the qml files from the resources
# for more details see the "Deployment Guides" in the Felgo Documentation

# during development, use the deploy_resources because you then get shorter compilation times (the qml files do not need to be compiled to the binary but are just copied)
# also, for quickest deployment on Desktop disable the "Shadow Build" option in Projects/Builds - you can then select "Run Without Deployment" from the Build menu in Qt Creator if you only changed QML files; this speeds up application start, because your app is not copied & re-compiled but just re-interpreted

    target_compile_definitions(delearner
        PRIVATE $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:QT_QML_DEBUG>)

    target_link_libraries(delearner PRIVATE Felgo)
    target_link_libraries(delearner PRIVATE Qt6::Core)
    target_link_libraries(delearner PRIVATE Qt6::Network)
    target_link_libraries(delearner PRIVATE Qt6::Sql)
    target_link_libraries(delearner PRIVATE delearner_core)

    if(IOS AND CMAKE_OSX_SYSROOT MATCHES "iphonesimulator")
        qt_import_plugins(delearner EXCLUDE Qt6::QFFmpegMediaPlugin)
    endif()

    if(IOS)
        target_link_libraries(delearner PRIVATE "-framework UIKit")
        set_target_properties(delearner PROPERTIES
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_GUI_IDENTIFIER ${PRODUCT_IDENTIFIER}
            MACOSX_BUNDLE_BUNDLE_NAME "DE Learner"
            MACOSX_BUNDLE_EXECUTABLE_NAME delearner
        )
    endif()
endif()

if(DELEARNER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# uncomment this line to add Felgo Hot Reload and use hot reloading with your custom C++ code
# for the remaining steps see here: https://felgo.com/custom-code-reload-app/
# find_package(FelgoHotReload REQUIRED)
# target_link_libraries(delearner PRIVATE FelgoHotReload)
