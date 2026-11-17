#!/usr/bin/env python3
"""Check GPU routing, event identity, merged hits and repeat-run isolation."""
import argparse
import csv
import json
import os
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET

import numpy as np


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('case', choices=('cerenkov', 'scintillation', 'zero', 'torch', 'nested', 'frozen_config'))
    args = parser.parse_args()
    binary = args.binary.resolve()
    repo = Path(__file__).resolve().parents[1]
    probe = subprocess.run([str(binary), '--list-gpus'], text=True, capture_output=True, timeout=30)
    assert probe.returncode == 0, f'GPU discovery failed: {probe.stdout}\n{probe.stderr}'
    devices = json.loads(probe.stdout.strip().splitlines()[-1])
    if len(devices) < 2:
        print('SKIP: requires two CUDA-visible GPUs')
        return 77
    uuids = [device['uuid'] for device in devices[:2]]
    scintillation = args.case == 'scintillation'
    torch_source = args.case in ('torch', 'nested', 'frozen_config')
    geometry = repo/'tests/geom'/('opticks_raindrop.gdml' if torch_source else
                                 'opticks_raindrop_with_scintillation.gdml' if scintillation else 'pfrich_min_FINAL.gdml')
    if args.case == 'nested':
        # Preserve the hit-producing geometry but wrap its outer daughter in
        # two assemblies, which Geant4 can read but cannot re-export to GDML.
        tree = ET.parse(geometry)
        structure = tree.find('structure')
        world_name = tree.find('setup/world').attrib['ref']
        world = next(v for v in structure.findall('volume') if v.attrib['name'] == world_name)
        daughter = world.find('physvol')
        world.remove(daughter)
        inner = ET.Element('assembly', name='nested_inner')
        inner.append(daughter)
        outer = ET.Element('assembly', name='nested_outer')
        ET.SubElement(ET.SubElement(outer, 'physvol'), 'volumeref', ref='nested_inner')
        index = list(structure).index(world)
        structure.insert(index, inner)
        structure.insert(index+1, outer)
        ET.SubElement(ET.SubElement(world, 'physvol'), 'volumeref', ref='nested_outer')
        # Assembly imprints rename physical volumes. Apply the detector surface
        # to the logical sensor instead of referencing the old physical names.
        border = structure.find('bordersurface')
        structure.remove(border)
        sensor = next(v for v in structure.findall('volume')
                      if any(a.attrib.get('auxtype') == 'SensDet' for a in v.findall('auxiliary')))
        skin = ET.SubElement(structure, 'skinsurface', name='nested_sensor', surfaceproperty=border.attrib['surfaceproperty'])
        ET.SubElement(skin, 'volumeref', ref=sensor.attrib['name'])
        geometry = Path.cwd()/'nested.gdml'
        tree.write(geometry, encoding='utf-8', xml_declaration=True)
    particle = 'geantino' if args.case == 'zero' else 'e-'
    baseline = {}
    for mode, selected in [('direct', uuids[:1]), ('pool', uuids[:1]), ('pool', uuids[1:]), ('auto', uuids)]:
        output = Path.cwd()/f'{args.case}-{mode}-{len(selected)}-{uuids.index(selected[0])}'
        output.mkdir(parents=True, exist_ok=True)
        for metadata_name in ('run.npy', 'run_meta.txt'):
            (output/metadata_name).unlink(missing_ok=True)
        config_dir = output/'config'
        config_dir.mkdir(exist_ok=True)
        config = json.loads((repo/'config/dev_timing.json').read_text())
        if not torch_source:
            config['torch'].update(pos=[0, 0, 0], mom=[0, .2, .8], numphoton=100)
        config['event'].update(event_mode='Minimal', output_dir=str(output))
        (config_dir/'multi.json').write_text(json.dumps(config))
        macro_lines = ['/run/verbose 0', '/run/initialize', '/run/eventModulo 1']
        if args.case == 'frozen_config':
            config['event']['hit_mask'] = 'EC'
            (config_dir/'replacement.json').write_text(json.dumps(config))
        if scintillation:
            macro_lines.append('/process/inactivate msc')
        artifacts = ('events.csv', 'events.manifest.json', 's_hits.npy', 'g_hits.npy')
        for run_id in range(2):
            destination = output/f'run-{run_id}'
            destination.mkdir(exist_ok=True)
            for artifact in artifacts:
                (destination/artifact).unlink(missing_ok=True)
            macro_lines += ['/run/beamOn 16', f"/control/shell mv {' '.join(artifacts)} {destination.name}"]
            if args.case == 'frozen_config' and run_id == 0:
                macro_lines.append('/control/shell mv config/replacement.json config/multi.json')
        macro = output/'run.mac'
        macro.write_text('\n'.join(macro_lines)+'\n')
        env = dict(os.environ, CUDA_VISIBLE_DEVICES=','.join(selected), SIMPHONY_CONFIG_DIR=str(config_dir), OPTICKS_HOME=str(repo))
        if scintillation:
            env['U4__CollectGenstep_G4Cerenkov_modified_DISABLE'] = '1'
        command = [str(binary), '--gdml', str(geometry), '--config', 'multi', '--macro', str(macro),
                   '--threads', '2', '--gpu-dispatch', mode, '--gpu-timeout', '60', '--seed', '42']
        if not torch_source:
            command += ['--particle', particle, '--momentum-gev-c', '0.0014219697254161214' if scintillation else '20',
                        '--multiplicity', '2' if scintillation else '10']
        with (output/'stdout.log').open('w') as log:
            result = subprocess.run(command, cwd=output, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=180)
        assert result.returncode == 0, (output/'stdout.log').read_text()[-7000:]
        if args.case == 'frozen_config':
            assert json.loads((config_dir/'multi.json').read_text())['event']['hit_mask'] == 'EC'
        if mode != 'direct':
            assert not any((output/name).exists() for name in ('run.npy', 'run_meta.txt')), 'Helper metadata escaped its private output directory'
        for run_id in range(2):
            destination = output/f'run-{run_id}'
            with (destination/'events.csv').open() as stream:
                rows = list(csv.DictReader(stream))
            assert [int(row['event_id']) for row in rows] == list(range(16))
            counts = [tuple(int(row[key]) for key in ('num_gensteps', 'num_photons', 'num_gpu_hits', 'num_g4_hits')) for row in rows]
            hits = np.load(destination/'s_hits.npy')
            g4_hits = np.load(destination/'g_hits.npy')
            assert hits.shape == (sum(count[2] for count in counts), 4, 4)
            assert g4_hits.shape == (sum(count[3] for count in counts), 4, 4)
            if args.case != 'zero':
                assert len(hits) > 0, 'Expected nonempty GPU hits to verify payload/result transport'
            if args.case == 'torch':
                assert len(g4_hits) > 0, 'Expected nonempty G4 torch hits'
            if run_id not in baseline:
                baseline[run_id] = counts, hits.view(np.uint32).copy(), g4_hits.view(np.uint32).copy()
            else:
                assert counts == baseline[run_id][0], f'{mode}: event counts differ from direct GPU control'
                assert np.array_equal(hits.view(np.uint32), baseline[run_id][1]), f'{mode}: GPU hits differ'
                assert np.array_equal(g4_hits.view(np.uint32), baseline[run_id][2]), f'{mode}: G4 hits differ'
            assigned = {row['gpu_uuid'] for row in rows if int(row['gpu_device_id']) >= 0}
            if args.case == 'zero':
                assert assigned == set()
                assert all(count == (0, 0, 0, 0) for count in counts)
            else:
                assert assigned == set(selected), f'Expected use of {selected}, got {assigned}'
                for uuid in assigned:
                    intervals = sorted((float(row['gpu_service_start_offset_s']), float(row['gpu_cleanup_end_offset_s']))
                                       for row in rows if row['gpu_uuid'] == uuid)
                    assert all(begin <= end for begin, end in intervals)
                    assert all(left[1] <= right[0] for left, right in zip(intervals, intervals[1:])), 'Same GPU processed two events at once'
            manifest = json.loads((destination/'events.manifest.json').read_text())
            assert {device['uuid'] for device in manifest['gpu_devices']} == set(selected)
            backend = 'direct' if mode == 'direct' else 'pool'
            assert manifest['gpu_dispatch'] == backend
            assert all(row['gpu_dispatch'] == backend for row in rows)
    print(f'{args.case}: single/two-GPU routing, hit equivalence and repeated runs passed')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
