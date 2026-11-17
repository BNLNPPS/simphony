#!/usr/bin/env python3
"""Check pool failures and the lifetime of parent outputs and geometry snapshots."""
import argparse
import json
import os
from pathlib import Path
import resource
import signal
import subprocess
import tempfile


BLOCKED_OUTPUT = {
    'parent_hits': 'g_hits.npy',
    'parent_timing': 'events.manifest.json',
    'parent_profile': 'profile.csv',
}
OUTPUTS = ('s_hits.npy', 'g_hits.npy', 'events.csv', 'events.manifest.json', 'profile.csv')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('phase', choices=('startup', 'request', 'finalization', *BLOCKED_OUTPUT,
                                          'geometry_parse', 'geometry_save', 'filtered_snapshot'))
    args = parser.parse_args()
    binary = args.binary.resolve()
    probe = subprocess.run([str(binary), '--list-gpus'], capture_output=True, text=True, timeout=30)
    if probe.returncode != 0:
        raise AssertionError(probe.stderr)
    devices = json.loads(probe.stdout.strip().splitlines()[-1])
    if not devices:
        print('SKIP: requires a CUDA-visible GPU')
        return 77
    repo = Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix='simg4ox-failure-') as directory:
        root = Path(directory)
        temp = root/'tmp'
        temp.mkdir()
        config_dir = root/'config'
        config_dir.mkdir()
        config = json.loads((repo/'config/dev_timing.json').read_text())
        config['event']['output_dir'] = str(root)
        config['event_timing']['profile'].update(enabled=args.phase != 'startup', output='profile.csv')
        (config_dir/'failure.json').write_text(json.dumps(config))
        macro = root/'run.mac'
        commands = ['/run/initialize', '/run/beamOn 2']
        if args.phase in ('request', 'finalization'):
            # The parent allocated its private job directory before entering the macro.
            commands.insert(0, '/control/shell cd gpu-workers-* && mkdir -p run-0/gpu-0/profile.csv')
        macro.write_text('\n'.join(commands) + '\n')
        env = dict(os.environ, TMPDIR=str(temp), SIMPHONY_CONFIG_DIR=str(config_dir), OPTICKS_HOME=str(repo),
                   CUDA_VISIBLE_DEVICES=devices[0]['uuid'])
        if args.phase == 'filtered_snapshot':
            env['CSGFoundry__SAVE_OPT'] = 'prim'
        gdml = root/'missing.gdml' if args.phase == 'geometry_parse' else repo/'tests/geom/opticks_raindrop.gdml'
        command = [str(binary), '--threads', '2', '--gpu-dispatch', 'pool', '--gpu-timeout',
                   '0.000001' if args.phase == 'startup' else '30',
                   '--gdml', str(gdml), '--config', 'failure', '--macro', str(macro)]
        if args.phase == 'finalization' or args.phase in BLOCKED_OUTPUT:
            # Empty events leave the first helper profile write to the final handshake.
            command += ['--particle', 'geantino', '--momentum-gev-c', '1']
        blocked = BLOCKED_OUTPUT.get(args.phase)
        previous = b'previous successful run'
        if blocked:
            (root/blocked).mkdir()
            (root/'s_hits.npy').write_bytes(previous)

        def limits():
            resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
            if args.phase == 'geometry_save':
                # Fail a real geometry array write with EFBIG, after config.json fits.
                signal.signal(signal.SIGXFSZ, signal.SIG_IGN)
                resource.setrlimit(resource.RLIMIT_FSIZE, (8192, 8192))

        result = subprocess.run(command, cwd=root, env=env, capture_output=True, text=True,
                                preexec_fn=limits, timeout=45)
        log = (result.stdout + result.stderr)[-8000:]
        assert not list(temp.glob('simg4ox-geometry-*')), 'Leaked temporary GPU geometry\n' + log
        if args.phase == 'filtered_snapshot':
            assert result.returncode == 0, log
            assert all((root/name).is_file() for name in OUTPUTS), log
            assert json.loads((root/'events.manifest.json').read_text())['event_count'] == 2
            return 0
        assert result.returncode != 0, f'Failed GPU {args.phase} reported success\n{log}'
        diagnosis = {
            'startup': 'deadline', 'request': 'profile.csv', 'finalization': 'profile.csv',
            'geometry_parse': 'missing.gdml', 'geometry_save': 'CSGFoundry/',
        }.get(args.phase, blocked)
        assert diagnosis in result.stdout + result.stderr, log
        for process in Path('/proc').glob('[0-9]*/cmdline'):
            try:
                command_line = process.read_bytes().split(b'\0')
            except (OSError, ProcessLookupError):
                continue
            assert not (command_line[1:2] == [b'--gpu-worker'] and any(str(root).encode() in arg for arg in command_line)), 'Failed job left a GPU helper alive'
        for name in OUTPUTS:
            if name == blocked:
                assert (root/name).is_dir()
            elif blocked and name == 's_hits.npy':
                assert (root/name).read_bytes() == previous, 'Failed run replaced previous hits'
            else:
                assert not (root/name).exists(), f'Failed run published {name}'
        assert not list(root.rglob('.simg4ox-output-*')), 'Leaked staged parent artifacts'
    print(f'GPU {args.phase} failure cleaned up')


if __name__ == '__main__':
    raise SystemExit(main())
