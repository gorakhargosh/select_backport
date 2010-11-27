PYTHON?=python2.5
TESTFLAGS=-v
TESTOPTS=
SETUPFLAGS=

all: inplace

# Build in-place
inplace:
	$(PYTHON) setup.py $(SETUPFLAGS) build_ext -i

.PHONY=build
build: 
	$(PYTHON) setup.py $(SETUPFLAGS) build

bdist_egg:
	$(PYTHON) setup.py $(SETUPFLAGS) bdist_egg

test_build: build 
	$(PYTHON) runtests.py $(TESTFLAGS) $(TESTOPTS)

test_inplace: inplace 
	$(PYTHON) runtests.py $(TESTFLAGS) $(TESTOPTS)
	
sdist: egg
	$(PYTHON) setup.py $(SETUPFLAGS) sdist --format=gztar

upload: 
	$(PYTHON) setup.py $(SETUPFLAGS) register sdist --format=gztar upload --sign

install:
	$(PYTHON) setup.py $(SETUPFLAGS) install

# What should the default be?
test: test_inplace

egg: bdist_egg

clean:
	find . \( -name '*.o' -o -name '*~' -o -name '*.so' -o -name '*.py[cod]' -o -name '*.dll' \) -exec rm -f {} \;
	rm -rf build

realclean: clean
	rm -f TAGS
	rm -rf dist
	$(PYTHON) setup.py clean -a

