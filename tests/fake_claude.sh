#!/bin/sh
# A stand-in for `claude -p`: it notes its arguments and input in
# $DROVER_TEST_DIR, and sums up any turn in one point about step 2; it gives
# points without words while $DROVER_TEST_DIR/empty exists, and fails like a
# usage limit while $DROVER_TEST_DIR/fail does.
echo "$@" >> "$DROVER_TEST_DIR/calls"
cat >> "$DROVER_TEST_DIR/input"
if [ -e "$DROVER_TEST_DIR/empty" ]; then
  echo '{"type":"result","is_error":false,"result":"{\"points\":[{\"kind\":\"done\"}]}"}'
  exit 0
fi
if [ -e "$DROVER_TEST_DIR/fail" ]; then
  echo '{"type":"result","is_error":true,"result":"Usage limit reached"}'
  exit 1
fi
echo '{"type":"result","is_error":false,"result":"Here: {\"points\":[{\"text\":\"Done it\",\"kind\":\"done\",\"step\":2}]}"}'
