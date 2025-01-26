/**
 * @file ut_ldp_utils.cpp
 * @author Liao YuanKai(savent_gate@outlook.com)
 * @version 0.1
 * @date 2025-01-26
 * @license This project is CLOSED SOURCE, All Rights Reserved
 * @copyright Copyright (c) 2025 SYSTech Co.
 */
#include <assert.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "ldp_utils.hpp"

using namespace systech::cif;

TEST(memcpy, basic)
{
    uint8_t src[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    uint8_t dst[8] = {0};

    ldp_memcpy::memcpy(dst, src, 8);
    EXPECT_THAT(dst, testing::ElementsAreArray(src));
}

TEST(memcpy, partial_copy)
{
    uint8_t src[12] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
                       0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C};
    uint8_t dst[12] = {0};

    ldp_memcpy::memcpy(dst, src, 10);

    for (int i = 0; i < 10; i++) {
        EXPECT_EQ(dst[i], src[i]);
    }
    for (int i = 10; i < 12; i++) {
        EXPECT_EQ(dst[i], 0);
    }
}
