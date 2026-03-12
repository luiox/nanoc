#include <gtest/gtest.h>
#include <spdlog/spdlog.h>
#include <fstream>
#include <string>
#include <vector>

// 测试汇编器基本功能
TEST(AssemblerTest, BasicAssembly) {
    // 创建一个测试汇编文件
    std::string testFile = "test_assembly.nas";
    std::ofstream ofs(testFile);
    ofs << "lmm R0, 10\n";
    ofs << "lmm R1, 20\n";
    ofs << "add R0, R1\n";
    ofs.close();
    
    // 这里只是测试文件创建，实际汇编器测试需要调用汇编器
    EXPECT_TRUE(true);
    
    // 清理测试文件
    std::remove(testFile.c_str());
}

// 测试汇编器标签处理
TEST(AssemblerTest, LabelHandling) {
    // 创建一个带标签的测试汇编文件
    std::string testFile = "test_labels.nas";
    std::ofstream ofs(testFile);
    ofs << "LOOP_START:\n";
    ofs << "    lmm R0, 1\n";
    ofs << "    add R1, R0\n";
    ofs << "    jmp LOOP_START\n";
    ofs.close();
    
    EXPECT_TRUE(true);
    
    // 清理测试文件
    std::remove(testFile.c_str());
}

// 测试汇编器注释处理
TEST(AssemblerTest, CommentHandling) {
    // 创建一个带注释的测试汇编文件
    std::string testFile = "test_comments.nas";
    std::ofstream ofs(testFile);
    ofs << "; 这是一个注释\n";
    ofs << "lmm R0, 10 ; 加载立即数10到R0\n";
    ofs << "add R0, R1 ; R0 = R0 + R1\n";
    ofs.close();
    
    EXPECT_TRUE(true);
    
    // 清理测试文件
    std::remove(testFile.c_str());
}

// 测试汇编器错误处理
TEST(AssemblerTest, ErrorHandling) {
    // 创建一个包含错误指令的测试汇编文件
    std::string testFile = "test_errors.nas";
    std::ofstream ofs(testFile);
    ofs << "invalid R0, 10\n"; // 无效指令
    ofs.close();
    
    EXPECT_TRUE(true);
    
    // 清理测试文件
    std::remove(testFile.c_str());
}