#!/bin/sh
# SPDX-License-Identifier: MIT
mkdir -p "$2"/html
if [ ! -d "$2"/.venv ]; then
    python -m venv "$2"/.venv
    "$2"/.venv/bin/python -m pip install -r "$1"/requirements.txt
fi
export PROJECT_ROOT="$(realpath "$(dirname "$1")")"
export DOCS_OUTPUT="$(realpath "$2")"
rm -rf "$2"/html/doxygen
"$2"/.venv/bin/sphinx-build "$1"/sphinx "$2"/html
