"""Check GPU option validation and the directories created for a pool job."""
import errno
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def check_options(binary):
    env = dict(os.environ, CUDA_VISIBLE_DEVICES="")
    for args, diagnosis in (
        (["--gpu-dispatch", "other"], "--gpu-dispatch must be auto, direct, or pool"),
        (["--gpu-dispatch", "pool", "--threads", "1"], "--gpu-dispatch pool requires --threads > 1"),
        (["--gpu-timeout", "0"], "--gpu-timeout must be positive and finite"),
        (["--gpu-timeout", "nan"], "--gpu-timeout must be positive and finite"),
        (["--gpu-timeout", "1e20"], "--gpu-timeout exceeds the representable clock interval"),
    ):
        result = subprocess.run([binary, *args], env=env, capture_output=True, text=True, timeout=15)
        assert result.returncode == 1 and diagnosis in result.stderr, (args, result.stderr)


def check_directories(binary, root, device):
    repo = Path(__file__).resolve().parents[1]
    config = json.loads((repo / "config/dev.json").read_text())
    config["event"]["output_dir"] = "$TEST_RESULTS/jobs"
    (root / "test.json").write_text(json.dumps(config))
    macro = root / "run.mac"
    macro.write_text("/run/initialize\n/run/beamOn 2\n")
    temp = root / "tmp"
    temp.mkdir()
    output = root / "results/jobs"
    output.mkdir(parents=True)
    env = dict(os.environ, TEST_RESULTS=str(output.parent), SIMPHONY_CONFIG_DIR=str(root),
               OPTICKS_HOME=str(repo), TMPDIR=str(temp), CUDA_VISIBLE_DEVICES=device, LC_ALL="C")
    args = [binary, "--gpu-dispatch", "pool", "--threads", "2", "--config", "test",
            "--macro", macro, "--gdml", repo / "tests/geom/opticks_raindrop.gdml"]

    # The shell and simg4ox have the same PID across exec. Seed the former job
    # path deterministically, as if a previous container had used this PID.
    shell = 'mkdir "$TEST_RESULTS/jobs/gpu-workers-$$" && exec "$@"'
    result = subprocess.run(["sh", "-c", shell, "sh", *args], cwd=root, env=env,
                            capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, (result.stdout + result.stderr)[-4000:]
    jobs = list(output.glob("gpu-workers-*"))
    assert len(jobs) == 2 and sum((job / "run-0/gpu-0").is_dir() for job in jobs) == 1, jobs
    assert all((output / name).is_file() for name in ("s_hits.npy", "g_hits.npy"))
    assert not (root / "$TEST_RESULTS").exists(), "created literal path token"
    assert not list(temp.glob("simg4ox-geometry-*")), "temporary geometry survived normal shutdown"

    missing = env.copy()
    del missing["TEST_RESULTS"]
    result = subprocess.run(args, cwd=root, env=missing, capture_output=True, text=True, timeout=60)
    assert result.returncode == 1 and "$TEST_RESULTS/jobs" in result.stderr, result.stderr[-4000:]
    assert not (root / "TEST_RESULTS").exists()

    # procfs is a valid directory, but the kernel rejects mkdir even for root.
    result = subprocess.run(args, cwd=root, env=dict(env, TMPDIR="/proc/self"),
                            capture_output=True, text=True, timeout=60)
    assert result.returncode == 1, (result.returncode, result.stderr[-4000:])
    assert "/proc/self/simg4ox-geometry-XXXXXX" in result.stderr, result.stderr[-4000:]
    assert os.strerror(errno.ENOENT) in result.stderr, result.stderr[-4000:]

    blocked = root / "blocked"
    blocked.touch()
    result = subprocess.run(args, cwd=root, env=dict(env, TEST_RESULTS=str(blocked)),
                            capture_output=True, text=True, timeout=30)
    assert result.returncode == 1 and str(blocked) in result.stderr, result.stderr[-4000:]
    assert not list(temp.glob("simg4ox-geometry-*")), "failed output setup leaked temporary geometry"


if __name__ == "__main__":
    executable = Path(sys.argv[1]).resolve()
    if sys.argv[2] == "options":
        check_options(executable)
    else:
        devices = json.loads(subprocess.check_output([executable, "--list-gpus"], text=True).splitlines()[-1])
        if not devices:
            sys.exit(77)
        with tempfile.TemporaryDirectory(prefix="simg4ox-startup-") as directory:
            check_directories(executable, Path(directory), devices[0]["uuid"])
