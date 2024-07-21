#ifndef NVM_STRING_HELPER_H
#define NVM_STRING_HELPER_H

#include <string.h>
#include <ctype.h>
#include <string>

static void
trim(std::string& str)
{
    // 去除字符串开头的空格和制表符
    str.erase(0, str.find_first_not_of(" \t"));

    // 去除字符串结尾的空格和制表符
    str.erase(str.find_last_not_of(" \t") + 1);
}

static void
trim(char * str)
{
    int start = 0;
    int end = strlen(str) - 1;

    // 寻找开头的空格和制表符
    while (isspace(str[start])) {
        start++;
    }

    // 寻找结尾的空格和制表符
    while (end >= start && isspace(str[end])) {
        end--;
    }

    // 移动字符到开头
    int i, j;
    for (i = start, j = 0; i <= end; i++, j++) {
        str[j] = str[i];
    }
    str[j] = '\0'; // 添加字符串结束符
}

static int
stricmp(const char * s1, const char * s2)
{
    for (; *s1 && *s2; s1++, s2++) {
        if (tolower(*s1) != tolower(*s2)) {
            return tolower(*s1) - tolower(*s2);
        }
    }
    return tolower(*s1) - tolower(*s2);
}

static int
strnicmp(const char * s1, const char * s2, size_t n)
{
    for (size_t i = 0; (i < n) && *s1 && *s2; s1++, s2++, i++) {
        if (tolower(*s1) != tolower(*s2)) {
            return tolower(*s1) - tolower(*s2);
        }
    }
    if (n > 0) {
        return tolower(*s1) - tolower(*s2);
    }
    return 0;
}

#endif // !NVM_STRING_HELPER_H