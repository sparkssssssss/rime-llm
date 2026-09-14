#!/usr/bin/env bash
# Local logic/unit tests for the rime-weasel-ai plugin.
# These run without librime or a Windows toolchain, so the risky streaming and
# parsing logic can be verified before spending a CI build.
#
# Usage: ./run_tests.sh   (from the plugin directory, or anywhere)
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
plugin_dir="$(dirname "$here")"
out_dir="${TMPDIR:-/tmp}/weasel_ai_tests"
mkdir -p "$out_dir"

echo "== 1/3 filter placement & streaming (real filter source + rime stubs) =="
g++ -std=c++17 -fsanitize=address -I "$plugin_dir/src" -I "$here/stubs" \
  -o "$out_dir/test_filter_order" \
  "$here/test_filter_order.cc" "$plugin_dir/src/weasel_ai_filter.cc"
"$out_dir/test_filter_order"

echo
echo "== 2/3 placement planner =="
g++ -std=c++17 -fsanitize=address -I "$plugin_dir/src" \
  -o "$out_dir/test_placement" "$here/test_placement.cc"
"$out_dir/test_placement"

echo
echo "== 3/3 json + chat-completion parsing =="
# weasel_ai_config.cc is exercised through a stub (it needs librime config),
# see test/stub_link.cc
g++ -std=c++17 -fsanitize=address \
  -I "$plugin_dir/src" -I "$plugin_dir/../../librime/src" -I "$here/rime_inc" \
  -o "$out_dir/test_weasel_ai" \
  "$here/test_weasel_ai.cc" \
  "$plugin_dir/src/weasel_ai_json.cc" \
  "$plugin_dir/src/weasel_ai_correction_service.cc" \
  "$here/stub_link.cc"
"$out_dir/test_weasel_ai"

echo
echo "all local tests passed"
