#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

// 测试spdlog是否正常工作
TEST(SpdlogTest, BasicLogging) {
    spdlog::info("Welcome to spdlog!");
    spdlog::error("Some error message with arg: {}", 1);
    
    EXPECT_TRUE(true);
}

// 测试gtest是否正常工作
TEST(GtestTest, BasicAssertion) {
    EXPECT_EQ(1 + 1, 2);
    EXPECT_TRUE(true);
    EXPECT_FALSE(false);
}

int main(int argc, char **argv) {
    // 初始化spdlog
    spdlog::set_level(spdlog::level::debug);
    
    // 初始化gtest
    ::testing::InitGoogleTest(&argc, argv);
    
    return RUN_ALL_TESTS();
}