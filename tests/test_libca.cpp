#include <gtest/gtest.h>
#include <libca/core/result.hpp>
#include <libca/str/utf8_string.hpp>
#include <spdlog/spdlog.h>
#include <string>

static ca::core::Result<int, std::string>
makeOk()
{
    return ca::core::Ok(42);
}

static ca::core::Result<int, std::string>
makeErr()
{
    return ca::core::Err(std::string("boom"));
}

TEST(LibcaTest, ResultOk)
{
    auto r = makeOk();
    ASSERT_TRUE(r.is_ok());
    EXPECT_EQ(r.unwrap(), 42);
}

TEST(LibcaTest, ResultErr)
{
    auto r = makeErr();
    ASSERT_TRUE(r.is_err());
}

TEST(LibcaTest, Utf8StringBasics)
{
    ca::str::Utf8String s("hello");
    EXPECT_EQ(s.byte_length(), 5);
    EXPECT_EQ(s.length(), 5);
    spdlog::info("libca Utf8String ok: {} bytes", s.byte_length());
}
