#!/usr/bin/env python3
"""
plot_divergent_tracks.py - VTK offscreen render of divergent GPU vs G4 photon tracks

Usage:
    python plot_divergent_tracks.py <gpu_record.npy> <g4_record.npy> <gpu_photon.npy> <g4_photon.npy> <geometry.gdml> [num_tracks] [output.png]

Renders the detector geometry (wireframe) with divergent photon tracks overlaid:
  Red  = GPU tracks
  Blue = G4 tracks
"""
import sys
import numpy as np
import vtk
import xml.etree.ElementTree as ET


def parse_gdml_boxes(gdml_path):
    """Parse GDML for box solids and physvol placements."""
    tree = ET.parse(gdml_path)
    root = tree.getroot()

    solids = {}
    for box in root.iter('box'):
        solids[box.get('name')] = (float(box.get('x')), float(box.get('y')), float(box.get('z')))

    physvols = {}
    for pv in root.iter('physvol'):
        name = pv.get('name', '')
        volref = pv.find('volumeref')
        posref = pv.find('position')
        if volref is not None and posref is not None:
            physvols[name] = (volref.get('ref'),
                              float(posref.get('x', '0')),
                              float(posref.get('y', '0')),
                              float(posref.get('z', '0')))

    volumes = {}
    for vol in root.iter('volume'):
        sref = vol.find('solidref')
        if sref is not None:
            volumes[vol.get('name')] = sref.get('ref')

    return solids, physvols, volumes


COLORS_MAP = {
    'pTPlayer': (0.6, 0, 0.8, 0.6),
    'pTPsubstrate': (0.2, 0.2, 0.9, 0.25),
    'BlueWLS': (0, 0.7, 0.9, 0.25),
    'ReflectiveFoilBack': (1, 0.5, 0, 0.6),
    'ReflectiveFoilEdge': (0.9, 0.5, 0, 0.4),
    'SiPMs': (0, 0.7, 0, 0.5),
}


def get_volume_color(name):
    for key, col in COLORS_MAP.items():
        if key in name:
            return col
    return (0.5, 0.5, 0.5, 0.1)


def add_geometry(renderer, solids, physvols, volumes):
    """Add GDML box volumes as wireframes."""
    for pvname, (vname, px, py, pz) in physvols.items():
        sname = volumes.get(vname)
        if sname and sname in solids:
            sx, sy, sz = solids[sname]
            cube = vtk.vtkCubeSource()
            cube.SetXLength(sx); cube.SetYLength(sy); cube.SetZLength(sz)
            cube.SetCenter(px, py, pz); cube.Update()
            mapper = vtk.vtkPolyDataMapper()
            mapper.SetInputData(cube.GetOutput())
            actor = vtk.vtkActor()
            actor.SetMapper(mapper)
            r, g, b, a = get_volume_color(pvname)
            actor.GetProperty().SetColor(r, g, b)
            actor.GetProperty().SetOpacity(a)
            actor.GetProperty().SetRepresentationToWireframe()
            actor.GetProperty().SetLineWidth(2.0)
            renderer.AddActor(actor)


def get_track(rec, pidx):
    """Extract valid steps from record array."""
    steps = rec[pidx]
    flagmasks = steps.view(np.uint32)[:, 3, 3]
    n = int(np.sum(flagmasks != 0))
    return steps[:n, 0, :3], n


def add_track(renderer, track, n, color, radius=1.5, opacity=0.8):
    """Add a polyline tube for a photon track."""
    if n < 2:
        return
    points = vtk.vtkPoints()
    for i in range(n):
        points.InsertNextPoint(float(track[i, 0]), float(track[i, 1]), float(track[i, 2]))
    line = vtk.vtkPolyLine()
    line.GetPointIds().SetNumberOfIds(n)
    for i in range(n):
        line.GetPointIds().SetId(i, i)
    lines = vtk.vtkCellArray()
    lines.InsertNextCell(line)
    pd = vtk.vtkPolyData()
    pd.SetPoints(points)
    pd.SetLines(lines)
    tube = vtk.vtkTubeFilter()
    tube.SetInputData(pd)
    tube.SetRadius(radius)
    tube.SetNumberOfSides(8)
    tube.Update()
    mapper = vtk.vtkPolyDataMapper()
    mapper.SetInputData(tube.GetOutput())
    actor = vtk.vtkActor()
    actor.SetMapper(mapper)
    actor.GetProperty().SetColor(*color)
    actor.GetProperty().SetOpacity(opacity)
    renderer.AddActor(actor)


def add_sphere(renderer, pos, color, radius=4):
    s = vtk.vtkSphereSource()
    s.SetCenter(float(pos[0]), float(pos[1]), float(pos[2]))
    s.SetRadius(radius)
    s.SetThetaResolution(16)
    s.SetPhiResolution(16)
    m = vtk.vtkPolyDataMapper()
    m.SetInputConnection(s.GetOutputPort())
    a = vtk.vtkActor()
    a.SetMapper(m)
    a.GetProperty().SetColor(*color)
    renderer.AddActor(a)


def add_label(renderer, text, x, y, color, size):
    txt = vtk.vtkTextActor()
    txt.SetInput(text)
    txt.GetPositionCoordinate().SetCoordinateSystemToNormalizedViewport()
    txt.GetPositionCoordinate().SetValue(x, y)
    txt.GetTextProperty().SetColor(*color)
    txt.GetTextProperty().SetFontSize(size)
    txt.GetTextProperty().SetBold(True)
    txt.GetTextProperty().SetFontFamilyToCourier()
    renderer.AddViewProp(txt)


def main():
    if len(sys.argv) < 6:
        print(f"Usage: {sys.argv[0]} <gpu_record.npy> <g4_record.npy> <gpu_photon.npy> <g4_photon.npy> <geometry.gdml> [num_tracks] [output.png]")
        sys.exit(1)

    gpu_rec = np.load(sys.argv[1])
    g4_rec = np.load(sys.argv[2])
    gpu_phot = np.load(sys.argv[3])
    g4_phot = np.load(sys.argv[4])
    gdml_path = sys.argv[5]
    num_tracks = int(sys.argv[6]) if len(sys.argv) > 6 else 20
    output = sys.argv[7] if len(sys.argv) > 7 else "divergent_tracks.png"

    # Find divergent photons
    gpu_flags = gpu_phot.view(np.uint32).reshape(-1, 4, 4)[:, 3, 0] & 0xFFFF
    g4_flags = g4_phot.view(np.uint32).reshape(-1, 4, 4)[:, 3, 0] & 0xFFFF
    divergent = np.where(gpu_flags != g4_flags)[0][:num_tracks]
    print(f"Plotting {len(divergent)} divergent photons: {divergent}")

    # Parse geometry
    solids, physvols, volumes = parse_gdml_boxes(gdml_path)

    # VTK setup
    renderer = vtk.vtkRenderer()
    renderer.SetBackground(0.95, 0.95, 0.95)
    renWin = vtk.vtkRenderWindow()
    renWin.SetOffScreenRendering(True)
    renWin.SetSize(1800, 1200)
    renWin.AddRenderer(renderer)

    add_geometry(renderer, solids, physvols, volumes)

    # Start marker
    add_sphere(renderer, (0, 0, 0), (0, 0.8, 0), 6)

    # Draw tracks
    for pidx in divergent:
        gpu_track, gpu_n = get_track(gpu_rec, pidx)
        g4_track, g4_n = get_track(g4_rec, pidx)

        add_track(renderer, gpu_track, gpu_n, (1, 0, 0), 1.2, 0.7)
        add_track(renderer, g4_track, g4_n, (0.1, 0.3, 1), 1.2, 0.7)

        if gpu_n >= 2:
            add_sphere(renderer, gpu_track[-1], (1, 0, 0), 4)
        if g4_n >= 2:
            add_sphere(renderer, g4_track[-1], (0.1, 0.3, 1), 4)

    # Labels
    add_label(renderer, f"{len(divergent)} divergent photons: GPU vs G4", 0.02, 0.94, (0, 0, 0), 18)
    add_label(renderer, "Red = GPU tracks", 0.02, 0.89, (0.8, 0, 0), 15)
    add_label(renderer, "Blue = G4 tracks", 0.02, 0.84, (0, 0.2, 0.8), 15)
    add_label(renderer, "Green = Start (0,0,0)", 0.02, 0.79, (0, 0.5, 0), 15)

    # Camera
    camera = renderer.GetActiveCamera()
    camera.SetPosition(600, -600, 500)
    camera.SetFocalPoint(0, 0, 150)
    camera.SetViewUp(0, 0, 1)
    camera.SetViewAngle(45)
    renderer.ResetCameraClippingRange()

    # Render and save
    renWin.Render()
    w2i = vtk.vtkWindowToImageFilter()
    w2i.SetInput(renWin)
    w2i.Update()
    writer = vtk.vtkPNGWriter()
    writer.SetFileName(output)
    writer.SetInputConnection(w2i.GetOutputPort())
    writer.Write()
    print(f"Saved {output}")


if __name__ == "__main__":
    main()
