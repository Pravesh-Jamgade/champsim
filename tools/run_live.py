#!/usr/bin/env python3
"""
┌─────────────────────────────────────────┐
│ Main Scheduler Process                  │
│ - Reads jobs.conf                       │
│ - Launches worker processes in parallel │
│ - Keeps at most MAX_PARALLEL running    │
│ - Handles Ctrl+C / SIGTERM cleanup      │
└──────────────┬──────────────────────────┘
               │   (spawns N worker processes)
               ▼
┌─────────────────────────────────────────┐
│ Worker Process (1 per job)              │
│ - Creates FIFO                          │
│ - Starts SIM (ChampSim) first (reader)  │
│ - Starts PIN second (writer)            │
│ - If either exits → kill the other      │
│ - Removes FIFO at end                   │
└──────────────┬──────────────────────────┘
               │   (spawns 2 child processes)
               ▼
┌─────────────────────────────────────────┐
│ SIM (ChampSim)  +  PIN (PinTool)        │
│ - Each started in its own process group │
│ - Can be killed as a unit (killpg)      │
└─────────────────────────────────────────┘

WHAT THIS SCRIPT DOES (WHY):
- You want PIN to *stream* trace records into a FIFO.
- You want ChampSim to *read* from that FIFO and simulate.
- You also want to run MANY such jobs, in parallel, without hanging.
- FIFOs can deadlock if one side exits. So:
    -> If SIM exits, kill PIN.
    -> If PIN exits, kill SIM.
- For long batches, you must also handle Ctrl+C / SIGTERM:
    -> kill all running worker processes
    -> workers kill their SIM/PIN children
    -> cleanup FIFOs
"""

# ----------------------------
# Standard library imports
# ----------------------------

import atexit  # used to register cleanup when Python exits normally
import os      # used for mkfifo, killpg, environment variables, CPU count, etc.
import re      # used for regex placeholder expansion and safe filenames
import shlex   # used to safely quote shell commands and CPU ranges
import signal  # used to catch SIGINT/SIGTERM and send SIGTERM/SIGKILL to children
import subprocess  # used to spawn worker processes and SIM/PIN processes
import sys     # used for argv, exit codes, stderr logging
import time    # used for delays, polling loops, scheduling checks
from dataclasses import dataclass  # convenient immutable-ish Job data holder
from datetime import datetime      # used to create timestamped log directory
from pathlib import Path           # robust filesystem paths
from typing import Dict, List, Optional, Tuple  # type hints for clarity


# ============================================================
# GLOBAL SHUTDOWN / CLEANUP STATE (per Python process)
# ============================================================
# IMPORTANT: This "global" state exists separately in:
#   - the main scheduler process
#   - each worker process
# Each process has its own memory. That is OK and intended.

_SHUTDOWN = False
# WHY: cleanup can be triggered multiple times (signal + atexit + exceptions),
# so we prevent running cleanup twice.

_CHILD_PG_PIDS: List[int] = []
# WHY: We run SIM and PIN with preexec_fn=os.setsid, which makes each child
# the leader of a *new process group*. Storing their PIDs lets us kill the
# whole group later with killpg (so no orphan child processes remain).

_FIFOS_TO_CLEAN: List[Path] = []
# WHY: Track all FIFOs created by this process so cleanup can remove them
# even if we crash or get SIGTERM.


def _kill_pgroup(pid: int, sig: int) -> None:
    # WHY: send a signal to the entire process group, not just one PID.
    # This prevents lingering child processes (PIN can spawn helpers).
    try:
        os.killpg(os.getpgid(pid), sig)  # kill process group of pid with sig
    except Exception:
        # WHY: if process already died or pgid isn't valid, ignore safely.
        pass


def _cleanup(reason: str = "exit") -> None:
    # WHY: centralized cleanup used by both:
    # - signals (Ctrl+C / SIGTERM)
    # - atexit (normal exit)
    global _SHUTDOWN  # we will update the global flag

    if _SHUTDOWN:
        # WHY: cleanup might be called twice; do nothing the second time.
        return

    _SHUTDOWN = True  # mark that cleanup has started
    print(f"[CLEANUP] {reason}", file=sys.stderr)  # log to stderr (visible even if stdout is buffered)

    # 1) Ask all children to stop nicely (SIGTERM)
    for pid in list(_CHILD_PG_PIDS):
        _kill_pgroup(pid, signal.SIGTERM)  # send SIGTERM to each child's process group

    # 2) Wait a short grace period for children to die naturally
    #    (we poll with kill(pid, 0) which checks if PID exists)
    for _ in range(20):  # 20 * 0.1s = ~2 seconds
        alive = 0  # count how many are still alive
        for pid in list(_CHILD_PG_PIDS):
            try:
                os.kill(pid, 0)  # does not kill; just checks if pid exists
                alive += 1
            except Exception:
                # pid is gone
                pass
        if alive == 0:
            break  # all dead -> stop waiting
        time.sleep(0.1)  # wait a bit before re-checking

    # 3) Force kill remaining children (SIGKILL)
    for pid in list(_CHILD_PG_PIDS):
        _kill_pgroup(pid, signal.SIGKILL)  # SIGKILL cannot be ignored

    # 4) Remove FIFOs (important to avoid stale files causing future deadlocks)
    for fp in list(_FIFOS_TO_CLEAN):
        try:
            if fp.exists():   # check if fifo path exists
                fp.unlink()   # delete the FIFO file
        except Exception:
            # WHY: ignore filesystem errors during emergency cleanup
            pass


def _signal_handler(signum, frame) -> None:
    # WHY: When the OS sends SIGINT/SIGTERM/etc., Python runs this handler.
    # We call _cleanup, then exit the process.
    try:
        sig_name = signal.Signals(signum).name  # readable signal name
    except Exception:
        sig_name = str(signum)  # fallback

    _cleanup(reason=f"signal:{sig_name}")  # stop children, delete FIFOs
    raise SystemExit(128 + int(signum))    # conventional exit code: 128+signal


def install_exit_handlers() -> None:
    # WHY: install cleanup hooks for BOTH normal exit and signal exit.

    atexit.register(_cleanup, "atexit")  # WHY: run cleanup on normal Python exit

    # Install handlers for common "terminate the program" signals.
    # (SIGKILL and SIGSTOP cannot be caught by any program.)
    for s in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP, signal.SIGQUIT, signal.SIGABRT):
        try:
            signal.signal(s, _signal_handler)  # route to our handler
        except Exception:
            # Some platforms may not allow all signals; ignore.
            pass

    # Optional: ignore SIGPIPE.
    # WHY: FIFO / pipe can break if the other side dies. Ignoring SIGPIPE prevents
    # the Python process from dying unexpectedly; we manage termination ourselves.
    try:
        signal.signal(signal.SIGPIPE, signal.SIG_IGN)
    except Exception:
        pass


# ============================================================
# JOB DATA MODEL
# ============================================================

@dataclass
class JobResolved:
    # WHY: store one fully-resolved job with concrete commands.
    name: str                 # unique name used for logs + {NAME}
    fifo: str                 # FIFO path used by both SIM and PIN
    sim_cmd: str              # FULL command string to run ChampSim side
    pin_cmd: str              # FULL command string to run Pin side
    vars: Dict[str, str]      # merged variable map (globals + job block)


# ============================================================
# CONFIG PARSING
# ============================================================

def parse_config_blocks(conf_path: Path) -> List[Dict[str, str]]:
    """
    Read jobs_new.conf and split into blocks separated by blank lines.
    Each block is a dict: {KEY: VALUE}.
    - Lines starting with # are comments.
    - Each non-empty line must be KEY=VALUE.
    """
    text = conf_path.read_text(encoding="utf-8")  # read entire config file

    blocks: List[Dict[str, str]] = []  # final list of blocks
    cur: Dict[str, str] = {}           # current block under construction

    def push():
        # WHY: helper to finalize current block when we hit a blank line
        nonlocal cur
        if cur:
            blocks.append(cur)  # add the finished block
            cur = {}            # reset for next block

    for raw in text.splitlines():  # iterate each line in config
        line = raw.strip()         # remove surrounding whitespace
        if not line:
            push()                 # blank line => end of current block
            continue
        if line.startswith("#"):
            continue               # comment => ignore
        if "=" not in line:
            raise ValueError(f"Bad line (expected KEY=VALUE): {raw}")  # enforce format

        k, v = line.split("=", 1)          # split only on the first '='
        k = k.strip()                       # normalize key
        v = v.strip().strip('"')            # normalize value and strip quotes
        cur[k] = v                          # store into current block dict

    push()  # push last block if file doesn't end with blank line
    return blocks  # return list of dicts


def split_globals_and_jobs(blocks: List[Dict[str, str]]) -> Tuple[Dict[str, str], List[Dict[str, str]]]:
    """
    Split blocks into:
      - globals_map: merged dict of all leading blocks without NAME
      - job_blocks: list of blocks that contain NAME (each is one job)
    """
    globals_dict: Dict[str, str] = {}  # merged global variables
    job_blocks: List[Dict[str, str]] = []  # raw job blocks

    seen_job = False  # once we see first NAME=, all subsequent blocks are jobs
    for b in blocks:
        if not seen_job and "NAME" not in b:
            globals_dict.update(b)  # merge global block values into globals
        else:
            seen_job = True
            if "NAME" not in b:
                # WHY: after jobs start, every block must be a job block (NAME required)
                raise ValueError(f"Job block missing NAME: {b}")
            job_blocks.append(b)  # store job block for later resolution

    return globals_dict, job_blocks


# ============================================================
# VARIABLE EXPANSION (supports {NAME}, {FIFO}, etc.)
# ============================================================

VAR_PATTERN = re.compile(r"\{([A-Za-z_][A-Za-z0-9_]*)\}")
# WHY: match patterns like {NAME} or {GRAPH_DIR}. This is used for substitution.

def expand_once(s: str, vars_map: Dict[str, str]) -> str:
    # WHY: replace each {VAR} once using vars_map
    def repl(m: re.Match) -> str:
        key = m.group(1)                 # the variable name inside { }
        return vars_map.get(key, m.group(0))  # if unknown, keep "{KEY}" unchanged
    return VAR_PATTERN.sub(repl, s)      # return substituted string


def expand_all(s: str, vars_map: Dict[str, str], max_iter: int = 10) -> str:
    # WHY: variables can be nested, e.g. GRAPH={GRAPH_DIR}/web.sg,
    # so we run expansion repeatedly until it stabilizes.
    prev = s
    for _ in range(max_iter):
        cur = expand_once(prev, vars_map)
        if cur == prev:
            return cur  # no changes => fully expanded
        prev = cur
    return prev  # stop after max_iter to avoid infinite loops


def resolve_job(globals_map: Dict[str, str], job_map: Dict[str, str]) -> JobResolved:
    # WHY: Merge globals + job overrides, expand all placeholders, build final commands.
    merged = dict(globals_map)  # start with global defaults
    merged.update(job_map)      # override with per-job values

    # Ensure job has NAME
    if "NAME" not in merged or not merged["NAME"].strip():
        raise ValueError(f"Job missing NAME: {job_map}")

    name = merged["NAME"].strip()  # normalize name
    merged["NAME"] = name          # store back normalized

    # If FIFO missing, derive from FIFO_DIR and NAME
    if "FIFO" not in merged or not merged["FIFO"].strip():
        fifo_dir = merged.get("FIFO_DIR", "").strip()
        if not fifo_dir:
            raise ValueError(f"Job '{name}' missing FIFO and no FIFO_DIR provided.")
        merged["FIFO"] = f"{fifo_dir}/{name}.fifo"

    # Expand placeholders in ALL merged variables (multiple passes to resolve chains)
    for _ in range(5):
        for k, v in list(merged.items()):
            merged[k] = expand_all(v, merged)

    fifo = merged["FIFO"]  # final fifo path

    # Build SIM command:
    # - If SIM= is present: it's "args only" => prefix with CHAMPSIM_BIN
    # - Else use SIM_CMD template (usually includes CHAMPSIM_BIN)
    champsim_bin = merged.get("CHAMPSIM_BIN", os.environ.get("CHAMPSIM_BIN", "./champsim"))
    if "SIM" in merged and merged["SIM"].strip():
        sim_args = expand_all(merged["SIM"].strip(), merged)
        sim_cmd = f"{champsim_bin} {sim_args}"
    else:
        sim_tpl = merged.get("SIM_CMD", "").strip()
        if not sim_tpl:
            raise ValueError(f"Job '{name}' missing SIM and no SIM_CMD template provided.")
        sim_cmd = expand_all(sim_tpl, merged)

    # Build PIN command:
    # - If PIN= present, use it directly (full command)
    # - Else use PIN_CMD template
    if "PIN" in merged and merged["PIN"].strip():
        pin_cmd = expand_all(merged["PIN"].strip(), merged)
    else:
        pin_tpl = merged.get("PIN_CMD", "").strip()
        if not pin_tpl:
            raise ValueError(f"Job '{name}' missing PIN and no PIN_CMD template provided.")
        pin_cmd = expand_all(pin_tpl, merged)

    return JobResolved(name=name, fifo=fifo, sim_cmd=sim_cmd, pin_cmd=pin_cmd, vars=merged)


# ============================================================
# EXECUTION HELPERS (CPU pinning, FIFO creation, logging)
# ============================================================

def cpu_range(slot: int, cpus_per_job: int) -> str:
    # WHY: Each job gets a contiguous CPU slice: slot*cpus_per_job ... end
    start = slot * cpus_per_job
    end = start + cpus_per_job - 1
    return f"{start}-{end}"  # taskset format


def mkfifo(path: Path) -> None:
    # WHY: Create a FIFO (named pipe) file at 'path'. FIFO is used to stream trace.
    path.parent.mkdir(parents=True, exist_ok=True)  # ensure directory exists

    # If old FIFO exists, remove it first so mkfifo doesn't fail
    try:
        if path.exists():
            path.unlink()
    except FileNotFoundError:
        pass

    os.mkfifo(path)  # create FIFO (named pipe)

    # Make FIFO readable/writable for convenience (optional)
    try:
        os.chmod(path, 0o666)
    except PermissionError:
        pass


def run_taskset(cmd: str, cpu: str, log_path: Path) -> subprocess.Popen:
    # WHY: Run a shell command pinned to CPU range, and save output to log file.
    # We use "bash -lc" so:
    # - your environment/path expansions behave like interactive shell
    # - long commands are easier (single string)
    full = f"taskset -c {shlex.quote(cpu)} bash -lc {shlex.quote(cmd)}"

    f = open(log_path, "wb")  # open log file for both stdout+stderr
    proc = subprocess.Popen(
        full,
        shell=True,                 # run using /bin/sh -c
        stdout=f,                   # redirect stdout to log
        stderr=subprocess.STDOUT,   # redirect stderr to same log
        preexec_fn=os.setsid        # IMPORTANT: create new session => new process group
    )

    proc._log_file = f             # store handle for later close
    _CHILD_PG_PIDS.append(proc.pid)  # track PID so cleanup can kill its process group
    return proc


def safe_filename(s: str) -> str:
    # WHY: NAME may contain spaces or weird chars; logs need safe filenames.
    return re.sub(r"[^A-Za-z0-9._-]+", "_", s)


def worker_run(job: JobResolved, slot: int, cpus_per_job: int, log_dir: Path) -> int:
    # WHY: Worker runs exactly one job: create FIFO, start SIM+PIN, manage termination.
    cpu = cpu_range(slot, cpus_per_job)  # compute cpu slice for this worker
    fifo_path = Path(job.fifo)           # fifo path object
    name_safe = safe_filename(job.name)  # safe name for log filenames

    # Determine log paths
    sim_log = log_dir / f"{name_safe}.sim.log"
    pin_log = log_dir / f"{name_safe}.pin.log"
    meta_log = log_dir / f"{name_safe}.meta.log"

    # Write metadata file to quickly see commands used for this job
    meta_log.write_text(
        f"NAME={job.name}\nCPU={cpu}\nFIFO={job.fifo}\n\nSIM_CMD={job.sim_cmd}\n\nPIN_CMD={job.pin_cmd}\n",
        encoding="utf-8"
    )

    # Popen handles for the two children
    sim_proc: Optional[subprocess.Popen] = None
    pin_proc: Optional[subprocess.Popen] = None

    # Delay between starting SIM and PIN (avoid FIFO open race)
    sim_delay = float(job.vars.get("SIM_START_DELAY", "0.2"))

    try:
        mkfifo(fifo_path)                 # create FIFO file
        _FIFOS_TO_CLEAN.append(fifo_path) # ensure cleanup removes it on crash/signal

        # Start SIM first (reader). It may block waiting for FIFO writer.
        sim_proc = run_taskset(job.sim_cmd, cpu, sim_log)

        # Give SIM time to initialize and open FIFO for reading
        time.sleep(sim_delay)

        # Start PIN second (writer). It will open FIFO for writing and start streaming.
        pin_proc = run_taskset(job.pin_cmd, cpu, pin_log)

        # Wait loop: whichever process exits first triggers killing the other.
        while True:
            sim_rc = sim_proc.poll()  # None if still running, else exit code
            pin_rc = pin_proc.poll()  # None if still running, else exit code

            if sim_rc is not None:
                # SIM exited first: kill PIN (otherwise PIN may keep running/hang)
                if pin_proc.poll() is None:
                    _kill_pgroup(pin_proc.pid, signal.SIGTERM)
                break

            if pin_rc is not None:
                # PIN exited first: kill SIM (otherwise SIM may block on FIFO forever)
                if sim_proc.poll() is None:
                    _kill_pgroup(sim_proc.pid, signal.SIGTERM)
                break

            time.sleep(0.2)  # avoid busy-spinning; poll 5 times/second

        # Reap both children; if they don't exit, escalate to SIGKILL
        for p in (sim_proc, pin_proc):
            if p is not None and p.poll() is None:
                try:
                    p.wait(timeout=5)  # wait up to 5 seconds to exit after SIGTERM
                except subprocess.TimeoutExpired:
                    _kill_pgroup(p.pid, signal.SIGKILL)  # force kill if still alive

        # Extract final return codes
        sim_rc = sim_proc.returncode if sim_proc and sim_proc.returncode is not None else 999
        pin_rc = pin_proc.returncode if pin_proc and pin_proc.returncode is not None else 999

        # Define job success: both exit with code 0
        return 0 if (sim_rc == 0 and pin_rc == 0) else 1

    except BaseException:
        # WHY: if worker is interrupted (Ctrl+C) or crashes, we must stop children.
        if pin_proc and pin_proc.poll() is None:
            _kill_pgroup(pin_proc.pid, signal.SIGTERM)
        if sim_proc and sim_proc.poll() is None:
            _kill_pgroup(sim_proc.pid, signal.SIGTERM)

        # Try to reap quickly, else SIGKILL
        for p in (sim_proc, pin_proc):
            if p and p.poll() is None:
                try:
                    p.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    _kill_pgroup(p.pid, signal.SIGKILL)

        raise  # re-raise so signal handler / SystemExit exits with correct code

    finally:
        # Close log file descriptors so logs flush properly
        for p in (sim_proc, pin_proc):
            if p is not None and hasattr(p, "_log_file"):
                try:
                    p._log_file.close()
                except Exception:
                    pass

        # Remove FIFO even on failure (best-effort)
        try:
            if fifo_path.exists():
                fifo_path.unlink()
        except Exception:
            pass


# ============================================================
# MAIN SCHEDULER (PARALLEL JOB CONTROL)
# ============================================================

def main() -> int:
    # WHY: install signal + atexit cleanup in the scheduler process
    install_exit_handlers()

    # Determine config file name from argv or default
    conf = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("jobs_new.conf")

    # Parse blocks from config file
    blocks = parse_config_blocks(conf)

    # Split into global defaults + job blocks
    globals_map, job_blocks = split_globals_and_jobs(blocks)

    # Resolve all jobs (expand variables, build concrete commands)
    jobs: List[JobResolved] = [resolve_job(globals_map, jb) for jb in job_blocks]

    # Fail fast if no jobs were found
    if not jobs:
        print("[ERROR] No jobs found.", file=sys.stderr)
        return 1

    # Read CPU parallelism settings from environment.
    # WHY env vars are convenient for one-off runs without editing code.
    total_cpus = int(os.environ.get("TOTAL_CPUS", os.cpu_count() or 1))
    cpus_per_job = int(os.environ.get("CPUS_PER_JOB", "8"))
    max_parallel_env = int(os.environ.get("MAX_PARALLEL", "0"))

    # Validate settings
    if cpus_per_job <= 0:
        print("[ERROR] CPUS_PER_JOB must be > 0", file=sys.stderr)
        return 1
    if total_cpus < cpus_per_job:
        print(f"[ERROR] TOTAL_CPUS({total_cpus}) < CPUS_PER_JOB({cpus_per_job})", file=sys.stderr)
        return 1

    # Compute max parallel jobs: how many slices we can fit
    computed_parallel = max(1, total_cpus // cpus_per_job)

    # If MAX_PARALLEL is provided, respect it; otherwise use computed value
    max_parallel = max_parallel_env if max_parallel_env > 0 else computed_parallel

    # Create a timestamped log directory for this run
    run_id = datetime.now().strftime("%Y%m%d_%H%M%S")
    log_dir = Path(f"logs_{run_id}")
    log_dir.mkdir(parents=True, exist_ok=True)

    # Print summary so you know what run is doing
    print(f"[INFO] jobs={len(jobs)}")
    print(f"[INFO] TOTAL_CPUS={total_cpus} CPUS_PER_JOB={cpus_per_job} MAX_PARALLEL={max_parallel}")
    print(f"[INFO] logs: {log_dir}")

    # "running" maps worker PID -> Popen object so we can poll/wait them
    running: Dict[int, subprocess.Popen] = {}

    # "slot_to_pid" maps CPU slot index -> worker PID currently using it
    slot_to_pid: Dict[int, int] = {}

    # next_idx points to the next job to schedule
    next_idx = 0

    # finished counts completed jobs
    finished = 0

    # Script path used to launch worker mode (same file, but different env)
    script_path = Path(__file__).resolve()

    def launch(job: JobResolved, slot: int) -> None:
        # WHY: spawn a new worker process which will run exactly one job.
        env = os.environ.copy()  # inherit environment

        # Tell the child it should run in worker mode
        env["PY_WORKER"] = "1"

        # Pass job data via environment variables (simple + robust)
        env["PY_JOB_NAME"] = job.name
        env["PY_JOB_FIFO"] = job.fifo
        env["PY_JOB_SIM"] = job.sim_cmd
        env["PY_JOB_PIN"] = job.pin_cmd
        env["PY_JOB_VARS_SIM_DELAY"] = job.vars.get("SIM_START_DELAY", "0.2")

        # Pass slot/cpu sizing to worker
        env["PY_SLOT"] = str(slot)
        env["PY_CPUS_PER_JOB"] = str(cpus_per_job)

        # Pass log directory so worker writes logs into same folder
        env["PY_LOG_DIR"] = str(log_dir)

        # Spawn worker process running this same script
        proc = subprocess.Popen(
            [sys.executable, str(script_path)],
            env=env,
            preexec_fn=os.setsid  # IMPORTANT: worker is its own process group for easy kill
        )

        # Record it in scheduler tracking dicts
        running[proc.pid] = proc
        slot_to_pid[slot] = proc.pid

        # Print launch message
        print(f"[LAUNCH] {job.name} slot={slot} cpu={cpu_range(slot, cpus_per_job)}")

    # ----------------------------
    # Initial fill: start up to MAX_PARALLEL workers
    # ----------------------------
    while next_idx < len(jobs) and len(slot_to_pid) < max_parallel:
        slot = len(slot_to_pid)     # assign the next free slot (0..max_parallel-1)
        launch(jobs[next_idx], slot)  # start worker
        next_idx += 1               # move to next job

    # ----------------------------
    # Scheduling loop: refill slots as workers finish
    # ----------------------------
    while finished < len(jobs):
        time.sleep(1.0)  # WHY: don't busy-loop; 1s granularity is fine for long jobs

        # Find all workers that have finished
        done = [pid for pid, proc in running.items() if proc.poll() is not None]

        for pid in done:
            rc = running[pid].wait()  # collect exit code (reaps process)

            # Determine which slot was freed
            freed_slot = None
            for s, p in list(slot_to_pid.items()):
                if p == pid:
                    freed_slot = s
                    del slot_to_pid[s]  # free that slot
                    break

            # Remove from running dict
            del running[pid]

            # Update finished count
            finished += 1

            # Log completion
            print(f"[DONE] pid={pid} rc={rc} finished={finished}/{len(jobs)}")

            # If there are jobs left, launch next into freed slot
            if freed_slot is not None and next_idx < len(jobs):
                launch(jobs[next_idx], freed_slot)
                next_idx += 1

    # ----------------------------
    # If any workers still exist (rare), wait them (safety)
    # ----------------------------
    for pid, proc in list(running.items()):
        rc = proc.wait()
        print(f"[DONE] pid={pid} rc={rc}")

    print("[DONE] all jobs complete.")
    return 0


# ============================================================
# ENTRYPOINT
# ============================================================

if __name__ == "__main__":
    # Worker mode: the scheduler spawns this script with PY_WORKER=1
    if os.environ.get("PY_WORKER") == "1":
        install_exit_handlers()  # WHY: worker must clean up SIM/PIN if killed

        # Reconstruct job from environment variables passed by scheduler
        job = JobResolved(
            name=os.environ["PY_JOB_NAME"],
            fifo=os.environ["PY_JOB_FIFO"],
            sim_cmd=os.environ["PY_JOB_SIM"],
            pin_cmd=os.environ["PY_JOB_PIN"],
            vars={"SIM_START_DELAY": os.environ.get("PY_JOB_VARS_SIM_DELAY", "0.2")},
        )

        # Slot tells which CPU range to pin to
        slot = int(os.environ["PY_SLOT"])
        cpus_per_job = int(os.environ["PY_CPUS_PER_JOB"])

        # Logs directory shared across workers for this run
        log_dir = Path(os.environ["PY_LOG_DIR"])

        # Run exactly one job and exit with that job's return code
        sys.exit(worker_run(job, slot, cpus_per_job, log_dir))

    # Scheduler mode: run the main loop (parse config, spawn workers)
    sys.exit(main())
