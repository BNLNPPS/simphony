"""Exercise helper-owned NumPy diagnostics across real Geant4 runs."""
import json
import os
from pathlib import Path
import resource
import signal
import socket
import struct
import subprocess
import sys
import tempfile


def run(binary, root, mode, device):
    repo = Path(__file__).resolve().parents[1]
    config = json.loads((repo / "config/dev_timing.json").read_text())
    config["event"].update(output_dir=str(root), event_reldir="event-data")
    (root / "diagnostics.json").write_text(json.dumps(config))
    macro = root / "run.mac"
    setup = ""
    if mode == "metadata":
        setup = "/control/shell cd gpu-workers-* && mkdir -p run-0/gpu-0/event-data/run.npy\n"
    elif mode in ("igs_directory_link", "igs_file_link"):
        shared = root / "shared"
        shared.mkdir()
        marker = shared / "gensteps.npy"
        marker.write_bytes(b"original")
        if mode == "igs_directory_link":
            setup = ("/control/shell cd gpu-workers-* && mkdir -p run-0/gpu-0 "
                     f"&& ln -s {shared} run-0/gpu-0/gensteps\n")
        else:
            setup = ("/control/shell cd gpu-workers-* && mkdir -p run-0/gpu-0/gensteps "
                     f"&& ln -s {marker} run-0/gpu-0/gensteps/gensteps.npy\n")
    macro.write_text(setup + "/run/initialize\n/run/beamOn 2\n/run/beamOn 2\n")
    original_dump = root / "original" / "gensteps.npy"
    env = dict(os.environ, SIMPHONY_CONFIG_DIR=str(root), CUDA_VISIBLE_DEVICES=device,
               OPTICKS_HOME=str(repo), QSim__SAVE_IGS_EVENTID="0", QSim__SAVE_IGS_PATH=str(original_dump))
    env.pop("SEvt__SAVE_NOTHING", None)
    args = [str(binary), "--threads", "2", "--gpu-dispatch", "pool", "--gpu-timeout", "30",
            "--gdml", str(repo / "tests/geom/opticks_raindrop.gdml"), "--config", "diagnostics", "--macro", str(macro)]
    with subprocess.Popen(args, cwd=root, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                          preexec_fn=lambda: resource.setrlimit(resource.RLIMIT_CORE, (0, 0))) as process:
        try:
            stdout, stderr = process.communicate(timeout=60)
        except subprocess.TimeoutExpired:
            process.kill()
            process.communicate()
            raise
    workers, = root.glob("gpu-workers-*")
    if mode != "igs":
        assert process.returncode != 0, f"helper accepted invalid diagnostic output for {mode}"
        assert ("run.npy" if mode == "metadata" else "gensteps") in stderr, (stdout + stderr)[-4000:]
        if mode != "metadata":
            assert marker.read_bytes() == b"original", "helper changed the shared diagnostic"
        assert not any((root / name).exists() for name in ("s_hits.npy", "g_hits.npy", "events.csv"))
    else:
        assert process.returncode == 0, (stdout + stderr)[-4000:]
        for run_id in (0, 1):
            dump = workers / f"run-{run_id}/gpu-0/gensteps/gensteps.npy"
            assert dump.is_file() and dump.read_bytes().startswith(b"\x93NUMPY"), f"missing run-local gensteps: {dump}"
        assert not original_dump.exists(), "helper wrote to the caller's original dump path"


def event_directory(binary, root, mode, devices):
    repo = Path(__file__).resolve().parents[1]
    config = json.loads((repo / "config/dev_timing.json").read_text())
    reldir, version, expected = {
        "traversal": ("../shared", "7", None),
        "token_traversal": ("nested/${VERSION:-0}", "../../shared", None),
        "symlink_traversal": ("link", "7", None),
        "nested_token": ("nested/ALL${VERSION:-0}", "7", "nested/ALL7"),
    }[mode]
    config["event"].update(output_dir=str(root), event_reldir=reldir)
    (root / "diagnostics.json").write_text(json.dumps(config))
    macro = root / "run.mac"
    setup = ""
    if mode == "symlink_traversal":
        shared = root / "shared"
        shared.mkdir()
        for device in range(len(devices)):
            output = f"run-0/gpu-{device}"
            setup += (f"/control/shell cd gpu-workers-* && mkdir -p {output} "
                      f"&& ln -s {shared} {output}/link\n")
    macro.write_text(setup + "/run/initialize\n/run/eventModulo 1\n/run/beamOn 16\n")
    env = dict(os.environ, SIMPHONY_CONFIG_DIR=str(root), CUDA_VISIBLE_DEVICES=','.join(devices),
               OPTICKS_HOME=str(repo), VERSION=version)
    env.pop("SEvt__SAVE_NOTHING", None)
    args = [str(binary), "--threads", "2", "--gpu-dispatch", "pool", "--gpu-timeout", "30",
            "--gdml", str(repo / "tests/geom/opticks_raindrop.gdml"), "--config", "diagnostics", "--macro", str(macro)]
    with subprocess.Popen(args, cwd=root, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                          preexec_fn=lambda: resource.setrlimit(resource.RLIMIT_CORE, (0, 0))) as process:
        try:
            stdout, stderr = process.communicate(timeout=60)
        except subprocess.TimeoutExpired:
            process.kill()
            process.communicate()
            raise
    workers, = root.glob("gpu-workers-*")
    if expected is None:
        assert not list(root.rglob('shared/run.npy')), "helper metadata escaped to the shared run directory"
        assert process.returncode != 0, "helper accepted an escaping event.event_reldir"
        assert 'event.event_reldir' in stderr, (stdout + stderr)[-4000:]
        if mode == "symlink_traversal":
            assert "inside the GPU helper directory" in stderr, stderr[-4000:]
    else:
        assert process.returncode == 0, (stdout + stderr)[-4000:]
        for device in range(len(devices)):
            output = workers / 'run-0' / f'gpu-{device}' / expected
            assert (output / 'run.npy').is_file(), f'missing worker metadata in {output}'
            assert (output / 'run_meta.txt').is_file(), f'missing worker metadata in {output}'


def truncated_header(worker, root, geometry, device):
    # Start the real helper at its private ABI boundary; stop sending mid-header.
    frame = struct.Struct("=IHHiiQQqqq")
    parent, child = socket.socketpair()
    with parent, child:
        parent.settimeout(30)
        env = dict(os.environ, CUDA_VISIBLE_DEVICES=device)
        config = Path(__file__).resolve().parents[1] / "config/dev.json"
        args = [str(worker), "--gpu-worker", str(config), str(geometry), str(root), "0", device, str(os.getpid())]
        pid = os.posix_spawn(str(worker), args, env, file_actions=[(os.POSIX_SPAWN_DUP2, child.fileno(), 3)])
        try:
            child.close()
            with parent.makefile("rb") as incoming:
                ready = frame.unpack(incoming.read(frame.size))
                assert ready[2] == 1 and ready[5] > 0 and ready[6] == 0, "helper did not report launch capacity"
                parent.sendall(b"\x55")
                parent.shutdown(socket.SHUT_WR)
                header = frame.unpack(incoming.read(frame.size))
                assert header[2] == 5, "real helper acknowledged a truncated request as a successful shutdown"
                assert b"truncated" in incoming.read(header[6]), "truncated request lost its diagnosis"
            _, status = os.waitpid(pid, 0)
            pid = None
            assert os.waitstatus_to_exitcode(status) != 0, "real worker exited successfully with a truncated frame"
        finally:
            if pid is not None:
                os.kill(pid, signal.SIGKILL)
                os.waitpid(pid, 0)


def main():
    binary = Path(sys.argv[1]).resolve()
    mode = sys.argv[2]
    devices = json.loads(subprocess.check_output([str(binary), "--list-gpus"], text=True).splitlines()[-1])
    if not devices:
        return 77
    with tempfile.TemporaryDirectory(prefix="simg4ox-diagnostics-") as d:
        if mode == "truncated":
            truncated_header(binary, Path(d), Path(sys.argv[3]).resolve(), devices[0]["uuid"])
        elif mode in ("traversal", "token_traversal", "symlink_traversal", "nested_token"):
            if len(devices) < 2:
                return 77
            event_directory(binary, Path(d), mode, [device["uuid"] for device in devices[:2]])
        else:
            run(binary, Path(d), mode, devices[0]["uuid"])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
