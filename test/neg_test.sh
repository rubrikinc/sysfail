#!/usr/bin/env bash

# Test for negative cases, set $EXPECT_CODE to the expected exit code

if [ -z "$EXPECT_CODE" ]; then
    echo "EXPECT_CODE is not set"
    exit 1
fi

"$@"
EXIT_CODE=$?

if [ "$EXIT_CODE" -eq "$EXPECT_CODE" ]; then
    exit 0
else
    echo "Expected $EXPECT_CODE but got $EXIT_CODE"
    exit 1
fi
