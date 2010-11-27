#!/usr/bin/env python2.5
"""Backport of the new select module with epoll and kqueue interface

The select26 extension is a backport of the new API functions of Python
2.6 for Python 2.3 to 2.5. It contains object oriented wrappers for epoll
(Linux 2.6) and kqueue/kevent (BSD).

>>> try:
...     import select26 as select
... except ImportError:
...     import select

>>> ep = select.epoll()
>>> kq = select.kqueue()

This release is based upon Python svn trunk r62498.
"""

import sys

try:
    from setuptools import setup
    from setuptools import Extension
except ImportError:
    from distutils.core import setup
    from distutils.core import Extension

MACROS = []

if "linux" in sys.platform:
    MACROS.append(("HAVE_EPOLL", 1))
    MACROS.append(("HAVE_SYS_EPOLL_H", 1))
elif "darwin" in sys.platform or "bsd" in sys.platform:
    MACROS.append(("HAVE_KQUEUE", 1))
    MACROS.append(("HAVE_SYS_EVENT_H", 1))
else:
    raise ValueError("Platform '%s' is not supported" % sys.platform)

if sys.version_info >= (2,6):
    raise ValueError("select26 is not required in Python 2.6+")

extensions = [
    Extension("select26", ["select26module.c"],
        define_macros = MACROS,
        )
    ]

setup(
    name = "select26",
    version = "0.1a3",
    description = __doc__[:__doc__.find('\n')].strip(),
    long_description = '\n'.join([line
                                  for line in __doc__.split('\n')[1:]]),
    author = "Christian Heimes",
    author_email = "christian@cheimes.de",
    download_url = "http://pypi.python.org/",
    license = "MIT",
    keywords = "select poll epoll kqueue",
    ext_modules = extensions,
    packages = ["tests"],
    include_package_data = True,
    platforms = ["Linux 2.6", "BSD", "Mac OS X"],
    provides = ["select26"],
    classifiers = (
        'Development Status :: 3 - Alpha',
        'Intended Audience :: Developers',
        'License :: OSI Approved :: MIT License',
        'Natural Language :: English',
        'Programming Language :: C',
        'Programming Language :: Python',
        'Operating System :: MacOS :: MacOS X',
        'Operating System :: POSIX :: BSD',
        'Operating System :: POSIX :: Linux',
        'Topic :: System :: Networking',
        )
    )

