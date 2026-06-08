cmake_minimum_required(VERSION 3.16)

project(delearner VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Felgo REQUIRED)


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

find_package(Qt6 REQUIRED COMPONENTS Core Sql Test)

set(DELEARNER_CORE_SOURCES
    Managers/DLDatabaseMaintenanceService.cpp
    Managers/DLDatabaseMaintenanceService.h
    Managers/DLDatabaseManager.cpp
    Managers/DLDatabaseManager.h
    Managers/DLGroupRepository.cpp
    Managers/DLGroupRepository.h
    Managers/DLGroupService.cpp
    Managers/DLGroupService.h
    Managers/DLLogging.cpp
    Managers/DLLogging.h
    Managers/DLQuizRepository.cpp
    Managers/DLQuizRepository.h
    Managers/DLQuizService.cpp
    Managers/DLQuizService.h
    Managers/DLQuizSession.cpp
    Managers/DLQuizSession.h
    Managers/DLReviewStatsRepository.cpp
    Managers/DLReviewStatsRepository.h
    Managers/DLWordRepository.cpp
    Managers/DLWordRepository.h
    Managers/DLWordService.cpp
    Managers/DLWordService.h
    Models/DLModelMappers.cpp
    Models/DLModelMappers.h
    Models/DLQuizQuestion.h
    Models/DLQuizResult.h
    Models/DLWord.h
    Models/DLWordForms.h
    Models/DLWordGroup.h
    Models/DLWordReviewStats.h
)

qt_add_library(delearner_core STATIC
    ${DELEARNER_CORE_SOURCES}
)

target_include_directories(delearner_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(delearner_core PUBLIC Qt6::Core Qt6::Sql)

qt_add_executable(delearner
    main.cpp
    Managers/DLAppStateManager.cpp
    Managers/DLAppStateManager.h
    ${QmlFiles}
    ${AssetsFiles}
)

felgo_configure_executable(delearner)

# Deploy resources to build folder/package directly
# comment for publishing
deploy_resources("${QmlFiles};${AssetsFiles}")

# Add QML files and resources to QML module to included them via QRC automatically:
qt_add_qml_module(delearner
    URI delearner
    VERSION 1.0
# uncomment for publishing:
#    QML_FILES ${QmlFiles}
#    RESOURCES ${AssetsFiles}
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
target_link_libraries(delearner PRIVATE Qt6::Sql)
target_link_libraries(delearner PRIVATE delearner_core)

enable_testing()
add_subdirectory(tests)

# uncomment this line to add Felgo Hot Reload and use hot reloading with your custom C++ code
# for the remaining steps see here: https://felgo.com/custom-code-reload-app/
# find_package(FelgoHotReload REQUIRED)
# target_link_libraries(delearner PRIVATE FelgoHotReload)
