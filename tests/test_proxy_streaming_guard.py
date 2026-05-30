#!/usr/bin/env python3
from pathlib import Path


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def function_block(source, start_marker, end_marker):
    start = source.index(start_marker)
    end = source.index(end_marker, start)
    return source[start:end]


def main():
    source = Path("appliance/src/phim_proxy_worker.c").read_text(encoding="utf-8")
    read_req = function_block(
        source,
        "static int phim_read_h1_request",
        "static int phim_send_block_ssl",
    )
    stream = function_block(
        source,
        "static int phim_inspect_response_stream_chunk",
        "static int phim_forward_response_fixed_length",
    )
    fixed = function_block(
        source,
        "static int phim_forward_response_fixed_length",
        "static int phim_forward_response_chunked",
    )
    chunked = function_block(
        source,
        "static int phim_forward_response_chunked",
        "static int phim_forward_response_until_close",
    )
    until_close = function_block(
        source,
        "static int phim_forward_response_until_close",
        "static int phim_forward_response(SSL *client_ssl",
    )

    require("Inspection unavailable" in source, "inspection unavailable response missing")
    require("verdict.action != PHIM_VERDICT_ALLOW" in source,
            "proxy must treat every non-allow verdict as no-forward")
    require("phim_h1_chunked_decoder_feed" in read_req,
            "chunked request bodies must be buffered and decoded before forwarding")
    require("phim_inspect_response_stream_chunk" in fixed,
            "fixed-length responses must use segment verdict barrier")
    require("phim_inspect_response_stream_chunk" in chunked,
            "chunked responses must use segment verdict barrier")
    require("phim_inspect_response_stream_chunk" in until_close,
            "until-close responses must use segment verdict barrier")
    require("PHIM_STREAM_INSPECT_OVERLAP_BYTES" in source,
            "streaming inspection must keep a cross-segment overlap window")
    require(stream.index("phim_inspect_response_part") < stream.index("phim_stream_tail_update"),
            "tail may only advance after a final allow")
    require(fixed.index("phim_inspect_response_stream_chunk") < fixed.index("phim_write_response_header_once"),
            "fixed-length segment verdict must happen before header forwarding")
    require(chunked.index("phim_inspect_response_stream_chunk") < chunked.index("phim_write_response_header_once"),
            "chunked segment verdict must happen before header forwarding")
    require(until_close.index("phim_inspect_response_stream_chunk") < until_close.index("phim_write_response_header_once"),
            "until-close segment verdict must happen before header forwarding")
    require('"body_chunk"' not in chunked,
            "chunked body chunks must not bypass the verdict barrier")
    require('"body_chunk"' not in until_close,
            "until-close body chunks must not bypass the verdict barrier")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
