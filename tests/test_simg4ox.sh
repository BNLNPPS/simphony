#!/usr/bin/env bash

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

simg4ox -g "$SCRIPT_DIR/geom/raindrop.gdml" -m "$SCRIPT_DIR/run.mac"
python3 "$SCRIPT_DIR/compare_ab.py"
