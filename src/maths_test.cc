#include <gtest/gtest.h>
#include "maths.h"

TEST(BasicTest, AddsNumbers) {
    int sum = add(3, 4);
    EXPECT_EQ(sum, 7);
}