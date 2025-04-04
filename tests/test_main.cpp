#include <gtest/gtest.h>

/**
 * @file test_main.cpp
 * @brief Google Test 프레임워크를 초기화하고 전체 테스트를 실행하는 파일.
 *
 * 이 파일에서는 GTest의 main 함수를 정의하며,
 * 모든 테스트 케이스를 수행합니다.
 */

 /**
  * @brief GTest 실행 진입점 함수.
  * @param argc 인자 개수
  * @param argv 인자 벡터
  * @return 테스트 결과 코드 (0: 성공, 1: 실패)
  */
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
