# Minimal makefile for Sphinx documentation
#

# You can set these variables from the command line, and also
# from the environment for the first two.
SPHINXOPTS    ?=
SOURCEDIR     = sphinx
BUILDDIR      = build
SPHINXBUILD   ?= $(BUILDDIR)/venv/bin/sphinx-build

html:

build/venv:
	@mkdir -p $@
	@python -m venv $@
	@$@/bin/python -m pip install -r requirements.txt

# Put it first so that "make" without argument is like "make help".
help: build/venv
	@$(SPHINXBUILD) -M help "$(SOURCEDIR)" "$(BUILDDIR)" $(SPHINXOPTS) $(O)

.PHONY: help Makefile

# Catch-all target: route all unknown targets to Sphinx using the new
# "make mode" option.  $(O) is meant as a shortcut for $(SPHINXOPTS).
%: build/venv Makefile
	mkdir -p $(BUILDDIR)
	@$(SPHINXBUILD) -M $@ "$(SOURCEDIR)" "$(BUILDDIR)" $(SPHINXOPTS) $(O)
