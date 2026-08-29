set(DELEARNER_TESTS
    TestDatabaseManager
    TestGroupRepository
    TestWordRepository
    TestReviewStatsRepository
    TestQuizRepository
    TestSyncEventSerializer
    TestAuthSessionStore
)

foreach(test_name IN LISTS DELEARNER_TESTS)
    qt_add_executable(${test_name}
        ${test_name}.cpp
    )

    target_link_libraries(${test_name}
        PRIVATE
            delearner_core
            Qt6::Test
            Qt6::Network
            Qt6::Sql
    )

    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

qt_add_executable(TestAppStateAuthBootstrap
    TestAppStateAuthBootstrap.cpp
    ../src/Managers/DLAppStateManager.cpp
    ../src/Services/DLDatabaseExportService.cpp
    ../src/Services/DLDatabaseExportService.h
)

target_link_libraries(TestAppStateAuthBootstrap
    PRIVATE
        delearner_core
        Qt6::Test
        Qt6::Network
        Qt6::Sql
)

add_test(NAME TestAppStateAuthBootstrap COMMAND TestAppStateAuthBootstrap)

find_program(PYTHON3_EXECUTABLE python3)
get_filename_component(DELEARNER_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
if(TARGET TestSyncEventSerializer)
    target_compile_definitions(TestSyncEventSerializer
        PRIVATE
            DELEARNER_ROOT_DIR="${DELEARNER_ROOT_DIR}"
    )
endif()
if(TARGET TestAuthSessionStore)
    target_compile_definitions(TestAuthSessionStore
        PRIVATE
            DELEARNER_ROOT_DIR="${DELEARNER_ROOT_DIR}"
    )
endif()
set(DELEARNER_CONTRACT_VALIDATOR "${DELEARNER_ROOT_DIR}/contracts/validate_contract_fixtures.py")
set(DELEARNER_CONTRACT_FIXTURE_DIR "${DELEARNER_ROOT_DIR}/contracts/fixtures/v1")

if(PYTHON3_EXECUTABLE AND EXISTS "${DELEARNER_CONTRACT_VALIDATOR}" AND IS_DIRECTORY "${DELEARNER_CONTRACT_FIXTURE_DIR}")
    add_test(
        NAME TestContractFixtures
        COMMAND "${PYTHON3_EXECUTABLE}" "${DELEARNER_CONTRACT_VALIDATOR}"
                --fixture-dir "${DELEARNER_CONTRACT_FIXTURE_DIR}"
    )
else()
    message(WARNING "Root contract fixture validator is unavailable; TestContractFixtures was not registered.")
endif()
