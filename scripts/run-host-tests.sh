#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${root}/build/host-tests"
mkdir -p "${build_dir}"

python3 "${root}/scripts/localization_catalog.py" --check

cxx="${CXX:-g++}"
cc="${CC:-gcc}"
cxxflags=(-std=c++20 -O2 -Wall -Wextra -Werror -I"${root}/app/src" -I"${root}/extern/libpeer/src")
cflags=(-std=c11 -O2 -Wall -Wextra -Werror -I"${root}/app/src" -I"${root}/extern/libpeer/src")
socket_libs=()
host_uname="$(uname -s)"
if [[ "${host_uname}" == MINGW* || "${host_uname}" == MSYS* ]]; then
  socket_libs=(-lws2_32)
fi

header_tests=(
  library_refresh_policy library_metadata_policy game_feed_policy card_viewport_policy command_memory_spill deferred_gpu_release
  signaling_registration_policy video_recovery_policy stream_maintenance_policy ready_frame_latency gallery_request_state ready_frame_metrics frame_header_pool input_scheduling_policy ingress_timing_policy ad_break_policy ad_report_policy app_launch_mode_policy atomic_file_replace
  audio_latency_policy audio_recovery_policy audio_gain_policy audio_rtp_utils audio_service_deadline frame_delivery_policy auth_policy cloud_session_policy
  proxy_policy controller_assignment_policy controller_delivery_policy
  controller_report_queue stream_adaptation_policy stream_status_policy
  controller_layout decode_queue_policy description_scroll_policy
  game_detail_policy gallery_prefetch_policy game_grid_navigation home_artwork_toast
  home_artwork_transaction home_record_policy home_shortcut_policy
  keyboard_input_policy keyboard_shortcuts autologin_choice library_sort media_input_policy membership_label
  membership_tier_policy native_auth_policy network_loop_policy network_quality_policy
  nro_shortcut_policy provider_picker_policy remote_candidate_policy server_location_policy server_route_policy server_probe_policy server_zone_policy server_assignment_policy
  session_error_policy session_recovery_policy software_yuv_upload stream_end_policy stream_overlay_policy
  stream_settings_policy subscription_display touch_mapping update_policy update_install_policy
  video_frame_timing video_quality_policy
)

run_cpp() {
  local name="$1"; shift
  local exe="${build_dir}/${name}"
  "${cxx}" "${cxxflags[@]}" "${root}/tests/${name}_test.cpp" "$@" -o "${exe}"
  "${exe}"
}

for name in "${header_tests[@]}"; do
  run_cpp "${name}"
done

run_cpp advertisement_source "${root}/app/src/advertisement_source.cpp" \
  "${root}/extern/borealis/library/lib/extern/tinyxml2/tinyxml2.cpp" \
  -I"${root}/extern/borealis/library/include" \
  -I"${root}/extern/borealis/library/include/borealis/extern/tinyxml2"

run_cpp game_feed_pager "${root}/app/src/localization.cpp" -I"${root}/tests/pager_stubs"

run_cpp app_state_feed "${root}/app/src/app_state.cpp"

run_cpp nvst_sdp "${root}/app/src/webrtc/nvst_sdp.cpp"
if [[ "${SKIP_JANSSON_TESTS:-0}" != "1" ]]; then
  run_cpp description_translation "${root}/app/src/description_translation.cpp" -ljansson -pthread
  run_cpp library_ownership "${root}/app/src/gfn/library_ownership.cpp" "${root}/app/src/app_state.cpp" -ljansson
  run_cpp game_feed_cache "${root}/app/src/gfn/game_feed_cache.cpp" -ljansson
  run_cpp library_catalog "${root}/app/src/gfn/library_catalog.cpp" -ljansson
  run_cpp session_cleanup "${root}/app/src/gfn/session_cleanup.cpp" "${root}/app/src/gfn/session_endpoints.cpp" -ljansson
  run_cpp session_endpoints "${root}/app/src/gfn/session_endpoints.cpp" -ljansson
  run_cpp signaling_registration "${root}/app/src/signaling_registration.cpp" -ljansson
  run_cpp signaling_diagnostics "${root}/app/src/signaling_diagnostics.cpp" -ljansson
  run_cpp cloud_session_protocol "${root}/app/src/gfn/cloud_session_protocol.cpp" -ljansson
  run_cpp server_route_history "${root}/app/src/gfn/server_route_history.cpp" -ljansson
  run_cpp server_zones "${root}/app/src/gfn/server_zones.cpp" -ljansson
  run_cpp stream_settings_storage "${root}/app/src/stream_settings.cpp" "${root}/app/src/localization.cpp" -ljansson
  run_cpp play_history_policy "${root}/app/src/play_history.cpp" -ljansson
fi

# Source-backed host tests exercise the real implementation units rather than
# only constexpr policy headers. Keep this list explicit so a missing source
# dependency fails CI instead of silently reducing coverage.
run_cpp ice_candidate_pair_policy
run_cpp image_url_policy "${root}/app/src/image_url_policy.cpp"
run_cpp gallery_image_cache "${root}/app/src/image_url_policy.cpp" -pthread \
  -I"${root}/tests/gallery_stubs" \
  -I"${root}/extern/borealis/library/include/borealis/extern/nanovg" \
  -I"${root}/extern/borealis/library/lib/extern/glfw/deps"
run_cpp localization "${root}/app/src/localization.cpp"
run_cpp ui_localization "${root}/app/src/ui_localization.cpp" "${root}/app/src/localization.cpp" -I"${root}/tests/ui_localization_stubs"
run_cpp nte_credentials "${root}/app/src/nte_credentials.cpp"
run_cpp game_autologin "${root}/app/src/game_autologin.cpp" "${root}/app/src/nte_credentials.cpp"
run_cpp epic_autologin "${root}/app/src/epic_autologin.cpp" "${root}/app/src/epic_screen_probe.cpp" "${root}/app/src/game_autologin.cpp" "${root}/app/src/nte_credentials.cpp"
run_cpp autologin_privacy "${root}/app/src/stream_diagnostics.cpp"
run_cpp runtime_journal "${root}/app/src/runtime_journal.cpp"
run_cpp performance_recorder "${root}/app/src/performance_recorder.cpp"
run_cpp websocket_message "${root}/app/src/WebSocketClient.cpp" -DCURL_STATICLIB   -I"${root}/tests/signaling_stubs" -I"${root}/extern/ffmpeg/include"
run_cpp signaling_send_result "${root}/app/src/signaling_client.cpp" -I"${root}/tests/signaling_stubs" -I"${root}/extern/ffmpeg/include"
run_cpp background_sampler -pthread
run_cpp interval_maximum -pthread
run_cpp transfer_frame_reuse -I"${root}/extern/ffmpeg/include"
run_cpp frame_queue_recycle "${root}/app/src/stream/ffmpeg/AVFrameHolder.cpp" -I"${root}/tests/stubs" -I"${root}/extern/ffmpeg/include" -pthread
run_cpp network_probe_budget -pthread
run_cpp network_resource_policy -pthread
run_cpp http_resource_retry "${root}/app/src/connection_debug.cpp" "${root}/app/src/proxy_diagnostics.cpp" \
  -I"${root}/extern/ffmpeg/include" -DCURL_STATICLIB -DSWITCHNOW_NETWORK_DEBUG=0 -pthread "${socket_libs[@]}"
run_cpp connection_debug "${root}/app/src/connection_debug.cpp" -DSWITCHNOW_NETWORK_DEBUG=1 -pthread
run_cpp connection_debug_disabled "${root}/app/src/connection_debug.cpp" -DSWITCHNOW_NETWORK_DEBUG=0 -pthread
run_cpp proxy_diagnostics "${root}/app/src/proxy_diagnostics.cpp"
run_cpp diagnostic_file "${root}/app/src/diagnostic_file.cpp"
run_cpp routing_diagnostics "${root}/app/src/routing_diagnostics.cpp" "${root}/app/src/diagnostic_file.cpp"
run_cpp diagnostic_report "${root}/app/src/connection_debug.cpp" "${root}/app/src/diagnostic_report.cpp" "${root}/app/src/performance_recorder.cpp" "${root}/app/src/diagnostic_file.cpp" -DSWITCHNOW_NETWORK_DEBUG=1 -pthread
run_cpp shortcut_worker "${root}/app/src/shortcut_worker.cpp" -pthread
run_cpp http_cancellation -pthread

# Optional real HTTP/SOCKS loopback tests (no account or external network).
if command -v pkg-config >/dev/null && pkg-config --exists libcurl && command -v python3 >/dev/null; then
  "${cxx}" "${cxxflags[@]}" "${root}/tests/proxy_http_client_probe.cpp" \
    "${root}/app/src/connection_debug.cpp" "${root}/app/src/proxy_diagnostics.cpp" \
    $(pkg-config --cflags --libs libcurl) -pthread "${socket_libs[@]}" -o "${build_dir}/proxy_http_client_probe"
  python3 "${root}/tests/proxy_http_integration_test.py"
  "${cxx}" "${cxxflags[@]}" "${root}/tests/image_http_session_probe.cpp" "${root}/app/src/image_http_session.cpp" \
    $(pkg-config --cflags --libs libcurl) -pthread "${socket_libs[@]}" -o "${build_dir}/image_http_session_probe"
  python3 "${root}/tests/image_http_session_integration_test.py"
else
  echo "Real proxy loopback tests skipped: libcurl development files or Python unavailable."
fi

stb_image_include="${root}/extern/borealis/library/include/borealis/extern/nanovg"
stb_write_include="${root}/extern/borealis/library/lib/extern/glfw/deps"
if [[ -f "${stb_image_include}/stb_image.h" && -f "${stb_write_include}/stb_image_write.h" ]]; then
  run_cpp shortcut_icon "${root}/app/src/shortcut_icon.cpp" \
    -I"${stb_image_include}" -I"${stb_write_include}" \
    -Wno-missing-field-initializers
fi

"${cc}" "${cflags[@]}" "${root}/tests/rtcp_nack_test.c" \
  "${root}/extern/libpeer/src/rtcp.c" "${socket_libs[@]}" -o "${build_dir}/rtcp_nack"
"${build_dir}/rtcp_nack"

# The production RTP source uses the POSIX socket/endian API supplied by
# devkitPro and Linux. MinGW/MSYS does not provide those headers, so these two
# source-backed tests run in the Ubuntu CI job. Platform-independent RTP
# recovery policy coverage still runs on every host below.
if [[ "${host_uname}" != MINGW* && "${host_uname}" != MSYS* ]]; then
  "${cc}" "${cflags[@]}" -D_GNU_SOURCE -ffunction-sections -fdata-sections "${root}/tests/udp_nonblocking_test.c" \
    "${root}/extern/libpeer/src/socket.c" -Wl,--gc-sections -o "${build_dir}/udp_nonblocking"
  "${build_dir}/udp_nonblocking"
  "${cc}" "${cflags[@]}" -D_GNU_SOURCE -ffunction-sections -fdata-sections \
    -Wno-sign-compare -Wno-unused-parameter "${root}/tests/agent_direct_poll_test.c" \
    "${root}/extern/libpeer/src/socket.c" -Wl,--gc-sections -o "${build_dir}/agent_direct_poll"
  "${build_dir}/agent_direct_poll"
else
  echo "Skipping POSIX socket integration tests on ${host_uname}; Ubuntu CI runs them."
fi

rtp_includes=()
if [[ "${host_uname}" == MINGW* || "${host_uname}" == MSYS* ]]; then
  rtp_includes=(-I"${root}/tests/network_posix_stubs")
  "${cc}" "${cflags[@]}" -Wno-pointer-sign -Wno-incompatible-pointer-types \
    -ffunction-sections -fdata-sections "${rtp_includes[@]}" \
    "${root}/tests/udp_batch_adapter_test.c" -Wl,--gc-sections \
    "${socket_libs[@]}" -o "${build_dir}/udp_batch_adapter"
  "${build_dir}/udp_batch_adapter"
fi
for name in rtp_adaptive_poll rtp_reorder rtp_feedback_equivalence; do
  "${cc}" "${cflags[@]}" "${rtp_includes[@]}" "${root}/tests/${name}_test.c" \
    "${root}/extern/libpeer/src/rtp.c" "${socket_libs[@]}" -o "${build_dir}/${name}"
  "${build_dir}/${name}"
done
"${cc}" "${cflags[@]}" "${root}/tests/udp_receive_batch_test.c" -o "${build_dir}/udp_receive_batch"
"${build_dir}/udp_receive_batch"
for name in sctp_association sctp_init_ack_regression; do
  "${cc}" "${cflags[@]}" "${rtp_includes[@]}" -Wno-unused-parameter -Wno-unused-function \
    -Wno-format -Wno-sign-compare "${root}/tests/${name}_test.c" \
    "${socket_libs[@]}" -o "${build_dir}/${name}"
  "${build_dir}/${name}"
done

"${cc}" "${cflags[@]}" "${root}/tests/rtp_recovery_policy_test.c" \
  -o "${build_dir}/rtp_recovery_policy"
"${build_dir}/rtp_recovery_policy"


"${cc}" "${cflags[@]}" "${root}/tests/dtls_datagram_queue_test.c" -o "${build_dir}/dtls_datagram_queue"
"${build_dir}/dtls_datagram_queue"

"${cc}" "${cflags[@]}" "${root}/tests/sctp_reliable_queue_test.c" -o "${build_dir}/sctp_reliable_queue"
"${build_dir}/sctp_reliable_queue"

"${cc}" "${cflags[@]}" "${root}/tests/sctp_handshake_retry_test.c" -o "${build_dir}/sctp_handshake_retry"
"${build_dir}/sctp_handshake_retry"

echo "Host suite passed (${#header_tests[@]} policies and available source-backed modules)."
if [[ "${SKIP_JANSSON_TESTS:-0}" == "1" ]]; then
  echo "CloudMatch/play-history JSON tests skipped: SKIP_JANSSON_TESTS=1."
fi
