#!/usr/bin/env python3

# Copyright (c) 2014-2024, The Monero Project
#
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without modification, are
# permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice, this list of
#    conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice, this list
#    of conditions and the following disclaimer in the documentation and/or other
#    materials provided with the distribution.
#
# 3. Neither the name of the copyright holder nor the names of its contributors may be
#    used to endorse or promote products derived from this software without specific
#    prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
# EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
# THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
# STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
# THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

import json
import os
import shutil
import socket
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request
from signal import SIGTERM


USAGE = "usage: libwallet_api_tests.py <builddir> <monerod_exe> <libwallet_api_tests_exe>"
MINED_BLOCKS = 90
# Client-side HTTP budget for the cheap daemon calls (/get_height and the one-block
# pulse), which answer in milliseconds on any host.
RPC_TIMEOUT = 30
# The initial chain is mined a chunk at a time instead of in one request, so no single
# HTTP request has to cover all MINED_BLOCKS blocks.
GENERATE_BLOCKS_CHUNK = 15
# Per-block client-side budget for a chunked "generateblocks" request; the request
# timeout is this many seconds times the number of blocks asked for. monerod adds a
# regtest block in 100-300 ms, so 20 s per block is generous even on a host under heavy
# contention.
GENERATE_BLOCKS_TIMEOUT_PER_BLOCK = 20.0
# Overrides GENERATE_BLOCKS_TIMEOUT_PER_BLOCK, which makes the timeout-recovery path of
# mine_to_height() directly testable: with a tiny value every block-generation request
# gives up client-side and the run must still reach the target height.
GENERATE_BLOCKS_TIMEOUT_ENV = "LIBWALLET_API_TESTS_BLOCK_TIMEOUT"
# Overall budget for the whole initial mining step, after which a genuinely broken or
# wedged daemon fails the run loudly instead of hanging until the ctest timeout.
MINING_TIMEOUT = 900


def reserve_ports(count):
    sockets = []
    ports = []
    try:
        for _ in range(count):
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.bind(("127.0.0.1", 0))
            sockets.append(s)
            ports.append(s.getsockname()[1])
    finally:
        for s in sockets:
            s.close()
    return ports


def wait_for_port(port, timeout=20):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.settimeout(0.2)
            if s.connect_ex(("127.0.0.1", port)) == 0:
                return
        time.sleep(0.1)
    raise RuntimeError("timed out waiting for port {}".format(port))


def rpc_json(rpc_port, path, payload, timeout=RPC_TIMEOUT):
    data = json.dumps(payload).encode("utf-8")
    request = urllib.request.Request(
        "http://127.0.0.1:{}{}".format(rpc_port, path),
        data=data,
        headers={"Content-Type": "application/json"},
    )
    with urllib.request.urlopen(request, timeout=timeout) as response:
        return json.loads(response.read().decode("utf-8"))


def get_height(rpc_port):
    response = rpc_json(rpc_port, "/get_height", {})
    return int(response["height"])


def wait_for_height(rpc_port, height, timeout=180):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            current_height = get_height(rpc_port)
            if current_height >= height:
                return current_height
        except (urllib.error.URLError, TimeoutError, KeyError):
            pass
        time.sleep(0.5)
    raise RuntimeError("timed out waiting for regtest height {}".format(height))


def generate_blocks_timeout(blocks):
    """Return the client-side HTTP budget for a "generateblocks" request.

    The budget scales with the number of blocks asked for, since that is what the daemon
    has to do before it answers. GENERATE_BLOCKS_TIMEOUT_ENV overrides the per-block
    figure; a malformed override is reported and ignored rather than aborting the run.
    """
    per_block = GENERATE_BLOCKS_TIMEOUT_PER_BLOCK
    override = os.environ.get(GENERATE_BLOCKS_TIMEOUT_ENV)
    if override is not None:
        try:
            per_block = float(override)
        except ValueError:
            print("Ignoring malformed {}={}".format(GENERATE_BLOCKS_TIMEOUT_ENV, override))
            sys.stdout.flush()
    return per_block * blocks


def generate_blocks(rpc_port, address, blocks, timeout=RPC_TIMEOUT):
    response = rpc_json(
        rpc_port,
        "/json_rpc",
        {
            "jsonrpc": "2.0",
            "id": "0",
            "method": "generateblocks",
            "params": {
                "wallet_address": address,
                "amount_of_blocks": blocks,
            },
        },
        timeout=timeout,
    )
    if "error" in response:
        raise RuntimeError("generateblocks failed: {}".format(response["error"]))
    return response


def read_height(rpc_port, deadline):
    """Read the regtest height, retrying transient client-side failures until deadline.

    /get_height is a cheap call, so a failure here is a contended host rather than a
    broken daemon; only the overall deadline turns it into a hard error.
    """
    while True:
        try:
            return get_height(rpc_port)
        except (TimeoutError, urllib.error.URLError, OSError, KeyError) as e:
            if time.monotonic() >= deadline:
                raise RuntimeError("failed to read regtest height: {}".format(e))
            print("Failed to read regtest height, retrying: {}".format(e))
            sys.stdout.flush()
            time.sleep(0.5)


def settle_height(rpc_port, deadline, quiet_seconds=3.0):
    """Return the regtest height once it has stopped advancing.

    Called after a block-generation request gave up client-side: the daemon carries on
    adding the blocks it was already asked for, so waiting for the chain to go quiet
    before the next request is issued keeps the recovery path from asking again for
    blocks that are already on their way.
    """
    height = read_height(rpc_port, deadline)
    unchanged_since = time.monotonic()
    while time.monotonic() < deadline:
        time.sleep(0.5)
        current_height = read_height(rpc_port, deadline)
        if current_height != height:
            height = current_height
            unchanged_since = time.monotonic()
        elif time.monotonic() - unchanged_since >= quiet_seconds:
            break
    return height


def mine_to_height(rpc_port, address, target_height, deadline):
    """Grow the regtest chain to target_height in chunks, driven by the daemon's height.

    Every iteration re-reads the height the daemon reports and asks only for the blocks
    that are genuinely still missing. That makes a client-side HTTP timeout non-fatal and
    keeps blocks from being over-generated: monerod keeps adding the blocks it was asked
    for after urllib gives up, so the next iteration simply observes the higher chain and
    requests less. The single overall deadline is what still fails a wedged daemon loudly.
    """
    height = read_height(rpc_port, deadline)
    while height < target_height:
        blocks = min(GENERATE_BLOCKS_CHUNK, target_height - height)
        try:
            generate_blocks(rpc_port, address, blocks, timeout=generate_blocks_timeout(blocks))
            height = read_height(rpc_port, deadline)
        except (TimeoutError, urllib.error.URLError, OSError) as e:
            # Client-side give-up only; the request itself is very likely still being
            # served, so recover from the height rather than failing the run.
            print("generateblocks({}) gave up client-side at height {}: {}".format(blocks, height, e))
            sys.stdout.flush()
            height = settle_height(rpc_port, deadline)
            print("Regtest height after recovery: {}/{}".format(height, target_height))
            sys.stdout.flush()
        if height < target_height and time.monotonic() >= deadline:
            raise RuntimeError(
                "timed out mining regtest blocks: reached height {} of {}".format(height, target_height))
    return height


def pulse_blocks(stop_event, rpc_port, address):
    while not stop_event.wait(1.0):
        try:
            generate_blocks(rpc_port, address, 1)
        except Exception as e:
            print("Failed to generate regtest block: {}".format(e))
            sys.stdout.flush()
            return


def stop_process(process):
    if process.poll() is not None:
        return
    try:
        process.send_signal(SIGTERM)
        process.wait(timeout=20)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()


def main():
    if len(sys.argv) != 4:
        print(USAGE)
        return 1

    builddir = os.path.abspath(sys.argv[1])
    monerod = os.path.abspath(sys.argv[2])
    test_exe = os.path.abspath(sys.argv[3])

    for executable in (monerod, test_exe):
        if not os.path.exists(executable):
            print("missing executable: {}".format(executable))
            return 1

    run_dir = os.path.join(builddir, "libwallet-api-tests-directory")
    wallets_dir = os.path.join(run_dir, "wallets")
    ringdb_dir = os.path.join(run_dir, "ringdb")
    data_dir = os.path.join(run_dir, "regtest")
    log_dir = os.path.join(builddir, "tests", "libwallet_api_tests")

    shutil.rmtree(run_dir, ignore_errors=True)
    os.makedirs(wallets_dir)
    os.makedirs(ringdb_dir)
    os.makedirs(data_dir)
    os.makedirs(log_dir, exist_ok=True)

    rpc_port, p2p_port, zmq_rpc_port, zmq_pub_port = reserve_ports(4)
    daemon_log_path = os.path.join(log_dir, "monerod-regtest.log")
    daemon_log = open(daemon_log_path, "ab")
    daemon = None
    block_pulse_stop = threading.Event()
    block_pulse = None

    try:
        daemon = subprocess.Popen(
            [
                monerod,
                "--regtest",
                "--fixed-difficulty",
                "1",
                "--p2p-bind-ip",
                "127.0.0.1",
                "--p2p-bind-port",
                str(p2p_port),
                "--rpc-bind-ip",
                "127.0.0.1",
                "--rpc-bind-port",
                str(rpc_port),
                "--zmq-rpc-bind-ip",
                "127.0.0.1",
                "--zmq-rpc-bind-port",
                str(zmq_rpc_port),
                "--zmq-pub",
                "tcp://127.0.0.1:{}".format(zmq_pub_port),
                "--non-interactive",
                "--offline",
                "--disable-dns-checkpoints",
                "--check-updates",
                "disabled",
                "--rpc-ssl",
                "disabled",
                "--data-dir",
                data_dir,
                "--log-level",
                "1",
            ],
            stdout=daemon_log,
            stderr=subprocess.STDOUT,
        )
        wait_for_port(rpc_port)

        env = os.environ.copy()
        env["WALLETS_ROOT_DIR"] = wallets_dir
        env["DAEMON_ADDRESS"] = "127.0.0.1:{}".format(rpc_port)
        env["LIBWALLET_API_TESTS_RINGDB_DIR"] = ringdb_dir

        generated = subprocess.check_output(
            [test_exe, "--generate-test-wallets"],
            cwd=run_dir,
            env=env,
            text=True,
        )
        generated_values = dict(
            line.split("=", 1)
            for line in generated.splitlines()
            if line.startswith(("miner_address=", "pulse_miner_address="))
        )
        miner_address = generated_values["miner_address"].strip()
        pulse_miner_address = generated_values["pulse_miner_address"].strip()
        # Mine the initial chain relative to the height the daemon reports, under one
        # overall deadline, so the step is bounded by the daemon's progress and not by
        # any single HTTP request completing.
        mining_deadline = time.monotonic() + MINING_TIMEOUT
        start_height = read_height(rpc_port, mining_deadline)
        target_height = start_height + MINED_BLOCKS
        print("Generating {} regtest blocks to {}: height {} -> {}".format(
            MINED_BLOCKS, miner_address, start_height, target_height))
        sys.stdout.flush()
        height = mine_to_height(rpc_port, miner_address, target_height, mining_deadline)
        print("Current regtest height: {}".format(height))
        sys.stdout.flush()
        wait_for_height(rpc_port, target_height)

        env.setdefault("GTEST_COLOR", "yes")
        gtest_filter = env.get("LIBWALLET_API_TESTS_GTEST_FILTER", "*")

        block_pulse = threading.Thread(target=pulse_blocks, args=(block_pulse_stop, rpc_port, pulse_miner_address))
        block_pulse.start()
        return subprocess.call(
            [test_exe, "--gtest_filter={}".format(gtest_filter)],
            cwd=run_dir,
            env=env,
        )
    finally:
        block_pulse_stop.set()
        if block_pulse is not None:
            block_pulse.join(timeout=10)
        if daemon is not None:
            stop_process(daemon)
        daemon_log.close()


if __name__ == "__main__":
    sys.exit(main())
