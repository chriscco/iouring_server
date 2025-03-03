#pragma once
#include <stdlib.h>
#include <cstdio> 
#include <ctype.h>

constexpr int group_id = 1337;

static void errif(bool condition, const char* message) {
    if (condition) {
        perror(message);
        exit(1);
    }
}

static void strtolower(char* str) {
    for (; *str; ++str) {
        *str = (char)tolower(*str);
    }
}