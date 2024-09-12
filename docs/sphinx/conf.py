# Configuration file for the Sphinx documentation builder.
#
# For the full list of built-in configuration values, see the documentation:
# https://www.sphinx-doc.org/en/master/usage/configuration.html

import os
import subprocess

# -- Project information -----------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#project-information

os.environ.setdefault('PROJECT_ROOT', os.path.join(os.getcwd(), '../..'))
os.environ.setdefault('DOCS_OUTPUT', os.path.join(os.getcwd(), '../build'))
for type in ('html', 'xml'):
    os.makedirs(os.path.join(os.environ['DOCS_OUTPUT'], f'doxygen/{type}'), exist_ok=True)
if subprocess.call(f'doxygen ../Doxyfile', shell=True) != 0:
    raise RuntimeError('Doxygen call failed')

project = 'ACEWS<br>Another C Embedded Web Server'
copyright = '2024 Jeff Kent and ACEWS Contributors'
author = 'Jeff Kent'
release = '0.1'

# -- General configuration ---------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#general-configuration

extensions = [
    'myst_parser',
]

templates_path = ['_templates']
exclude_patterns = []

# -- Options for HTML output -------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#options-for-html-output

html_theme = 'sphinx_rtd_theme'
html_static_path = ['_static']
html_extra_path = [os.path.join(os.environ['DOCS_OUTPUT'], 'doxygen/html')]
