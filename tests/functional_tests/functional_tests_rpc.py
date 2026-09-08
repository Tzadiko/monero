#!/usr/bin/env python3

import sys
import time
import subprocess
from signal import SIGTERM
import socket
import string
import os
import time

USAGE = 'usage: functional_tests_rpc.py <python> <srcdir> <builddir> [<tests-to-run> | all]'
DEFAULT_TESTS = [
  'address_book', 'bans', 'blockchain', 'cold_signing', 'daemon_info', 'get_output_distribution',
  'http_digest_auth', 'integrated_address', 'k_anonymity', 'mining', 'multisig', 'p2p', 'proofs',
  'sign_message', 'transfer', 'txpool', 'uri', 'validate_address', 'wallet'
]
try:
  python = sys.argv[1]
  srcdir = sys.argv[2]
  builddir = sys.argv[3]
except:
  print(USAGE)
  sys.exit(1)

try:
  sys.argv[4]
except:
  print(USAGE)
  print('Available tests: ' + ', '.join(DEFAULT_TESTS))
  print('Or run all with "all"')
  sys.exit(0)

try:
  tests = sys.argv[4:]
  if tests == ['all']:
    tests = DEFAULT_TESTS
except:
  tests = DEFAULT_TESTS

# a main offline monerod, does most of the tests
# two local online monerods connected to each other
N_MONERODS = 5

# 4 wallets connected to the main offline monerod
# 1 wallet connected to the first local online monerod
# 1 offline wallet
N_WALLETS = 7

WALLET_DIRECTORY = builddir + "/functional-tests-directory"
FUNCTIONAL_TESTS_DIRECTORY = builddir + "/tests/functional_tests"
DIFFICULTY = 10

monerod_base = [builddir + "/bin/monerod", "--regtest", "--fixed-difficulty", str(DIFFICULTY), "--p2p-bind-port", "monerod_p2p_port", "--rpc-bind-port", "monerod_rpc_port", "--zmq-rpc-bind-port", "monerod_zmq_port", "--zmq-pub", "monerod_zmq_pub", "--non-interactive", "--disable-dns-checkpoints", "--check-updates", "disabled", "--rpc-ssl", "disabled", "--data-dir", "monerod_data_dir", "--log-level", "1", "--rpc-max-connections-per-private-ip", "100", "--rpc-max-connections", "100"]

monerod_extra = [
  ["--offline"],
  ["--offline"],
  ["--add-exclusive-node", "127.0.0.1:18283"],
  ["--add-exclusive-node", "127.0.0.1:18282"],
  ["--rpc-login", "md5_lover:Z1ON0101", "--offline"],
]
wallet_base = [builddir + "/bin/monero-wallet-rpc", "--wallet-dir", WALLET_DIRECTORY, "--rpc-bind-port", "wallet_port", "--rpc-ssl", "disabled", "--daemon-ssl", "disabled", "--log-level", "1", "--allow-mismatched-daemon-version"]
wallet_extra = [
  ["--daemon-port", "18180", "--disable-rpc-login"],
  ["--daemon-port", "18180", "--disable-rpc-login"],
  ["--daemon-port", "18180", "--disable-rpc-login"],
  ["--daemon-port", "18180", "--disable-rpc-login"],
  ["--daemon-port", "18182", "--disable-rpc-login"],
  ["--offline", "--disable-rpc-login"],
  ["--daemon-port", "18184", "--daemon-login", "md5_lover:Z1ON0101", "--rpc-login", "kyle:reveille"],
]

command_lines = []
processes = []
outputs = []
ports = []

for i in range(N_MONERODS):
  command_lines.append([str(18180+i) if x == "monerod_rpc_port" else str(18280+i) if x == "monerod_p2p_port" else str(18380+i) if x == "monerod_zmq_port" else "tcp://127.0.0.1:" + str(18480+i) if x == "monerod_zmq_pub" else builddir + "/functional-tests-directory/monerod" + str(i) if x == "monerod_data_dir" else x for x in monerod_base])
  if i < len(monerod_extra):
    command_lines[-1] += monerod_extra[i]
  outputs.append(open(FUNCTIONAL_TESTS_DIRECTORY + '/monerod' + str(i) + '.log', 'a+'))
  ports.append(18180+i)

for i in range(N_WALLETS):
  command_lines.append([str(18090+i) if x == "wallet_port" else x for x in wallet_base])
  if i < len(wallet_extra):
    command_lines[-1] += wallet_extra[i]
  outputs.append(open(FUNCTIONAL_TESTS_DIRECTORY + '/wallet' + str(i) + '.log', 'a+'))
  ports.append(18090+i)

print('Starting servers...')
try:
  PYTHONPATH = os.environ['PYTHONPATH'] if 'PYTHONPATH' in os.environ else ''
  if len(PYTHONPATH) > 0:
    PYTHONPATH += ':'
  PYTHONPATH += srcdir + '/../../utils/python-rpc'
  os.environ['PYTHONPATH'] = PYTHONPATH
  os.environ['WALLET_DIRECTORY'] = WALLET_DIRECTORY
  os.environ['FUNCTIONAL_TESTS_DIRECTORY'] = FUNCTIONAL_TESTS_DIRECTORY
  os.environ['SOURCE_DIRECTORY'] = srcdir
  os.environ['PYTHONIOENCODING'] = 'utf-8'
  os.environ['DIFFICULTY'] = str(DIFFICULTY)
  os.environ['MAKE_TEST_SIGNATURE'] = FUNCTIONAL_TESTS_DIRECTORY + '/make_test_signature'
  os.environ['SEEDHASH_EPOCH_BLOCKS'] = "8"
  os.environ['SEEDHASH_EPOCH_LAG'] = "4"
  if not 'MINING_SILENT' in os.environ:
    os.environ['MINING_SILENT'] = "1"

  for i in range(len(command_lines)):
    #print('Running: ' + str(command_lines[i]))
    processes.append(subprocess.Popen(command_lines[i], stdout = outputs[i]))
except Exception as e:
  print('Error: ' + str(e))
  sys.exit(1)

def kill():
  for i in range(len(processes)):
    try: processes[i].send_signal(SIGTERM)
    except: pass

# wait for error/startup
#
# Every server gets its OWN startup budget. A single deadline shared by the
# sequential probes of all the servers made each one inherit whatever the
# previous ones had consumed, so one slow starter (a wallet generating its
# self-signed certificate, or any process descheduled on a loaded host) aborted
# the whole run before a single scenario had been given a chance to execute.
# The budget is deliberately generous and can be overridden for testing.
DEFAULT_STARTUP_TIMEOUT = 60
STARTUP_TIMEOUT_VAR = 'MONERO_FUNCTIONAL_TESTS_STARTUP_TIMEOUT'
# cap for a single connect_ex, so the loop keeps polling the process state
# instead of blocking for the whole remaining budget in one syscall
PROBE_TIMEOUT = 1

startup_timeout = DEFAULT_STARTUP_TIMEOUT
if STARTUP_TIMEOUT_VAR in os.environ:
  try:
    startup_timeout = float(os.environ[STARTUP_TIMEOUT_VAR])
  except ValueError:
    print('Ignoring invalid ' + STARTUP_TIMEOUT_VAR + ': ' + os.environ[STARTUP_TIMEOUT_VAR] + ', using ' + str(DEFAULT_STARTUP_TIMEOUT) + ' s')
    startup_timeout = DEFAULT_STARTUP_TIMEOUT

startup_start = time.monotonic()
slowest_port = ports[0]
slowest_wait = 0
for i in range(len(ports)):
  port = ports[i]
  addr = ('127.0.0.1', port)
  port_start = time.monotonic()
  deadline = port_start + startup_timeout
  delay = 0
  while True:
    time.sleep(delay)
    # a process which exited will never open its port: report it now with its
    # exit status instead of waiting out the budget and blaming the port
    status = processes[i].poll()
    if status is not None:
      print('Process for port ' + str(port) + ' (' + os.path.basename(command_lines[i][0]) + ') exited before opening its port, exit status: ' + str(status) + ', see ' + outputs[i].name)
      kill()
      sys.exit(1)
    timeout = deadline - time.monotonic()
    if timeout <= 0:
      print('Failed to start wallet or daemon, last port checked: ' + str(port))
      print('Waited ' + '%.2f' % (time.monotonic() - port_start) + ' s for port ' + str(port) + ' (per server startup timeout: ' + '%g' % startup_timeout + ' s, override with ' + STARTUP_TIMEOUT_VAR + ')')
      kill()
      sys.exit(1)
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
      s.settimeout(min(timeout, PROBE_TIMEOUT))
      err = s.connect_ex(addr)
    if err == 0:
      break
    delay = .1
  wait = time.monotonic() - port_start
  if wait > slowest_wait:
    slowest_wait = wait
    slowest_port = port

print('Started ' + str(len(ports)) + ' servers in ' + '%.2f' % (time.monotonic() - startup_start) + ' s (slowest: port ' + str(slowest_port) + ' at ' + '%.2f' % slowest_wait + ' s, per server startup timeout: ' + '%g' % startup_timeout + ' s)')
sys.stdout.flush()

# online daemons need some time to connect to peers to be ready
time.sleep(2)

PASS = []
FAIL = []
for test in tests:
  try:
    print('[TEST STARTED] ' + test)
    sys.stdout.flush()
    cmd = [python, srcdir + '/' + test + ".py"]
    subprocess.check_call(cmd)
    PASS.append(test)
    print('[TEST PASSED] ' + test)
  except:
    FAIL.append(test)
    print('[TEST FAILED] ' + test)
    pass

print('Stopping servers...')
kill()

# Wait up to 10 seconds for each process, then force termination.
for p in processes:
  try:
    p.wait(timeout=10)
  except subprocess.TimeoutExpired:
    print('Failed to stop process ' + str(p.pid) + ', killing it')
    p.kill()
    p.wait()

if len(FAIL) == 0:
  print('Done, ' + str(len(PASS)) + '/' + str(len(tests)) + ' tests passed')
else:
  print('Done, ' + str(len(FAIL)) + '/' + str(len(tests)) + ' tests failed: ' + ', '.join(FAIL))

sys.exit(0 if len(FAIL) == 0 else 1)
