set(DELEARNER_TESTS
    TestDatabaseManager
    TestGroupRepository
    TestWordRepository
    TestReviewStatsRepository
    TestQuizRepository
)

foreach(test_name IN LISTS DELEARNER_TESTS)
    qt_add_executable(${test_name}
        ${test_name}.cpp
    )

    target_link_libraries(${test_name}
        PRIVATE
            delearner_core
            Qt6::Test
            Qt6::Sql
    )

    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()
