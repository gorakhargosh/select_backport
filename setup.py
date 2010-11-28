#!/usr/bin/env python2.5
"""Backport of the new select module with epoll and kqueue interface

The select_backport extension is a backport of the new API functions of Python
2.7/SVN for Python 2.3 to 2.6. It contains object oriented wrappers for epoll
(Linux 2.6) and kqueue/kevent (BSD).

>>> try:
...     import select_backport as select
... except ImportError:
...     import select

>>> ep = select.epoll()
>>> kq = select.kqueue()

This release is based upon Python svn.

NOTE: I made this package because python2.5 and python2.6 lacked features
I'm using from select.kqueue and the select26 package isn't being maintained.
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

#Python2.6 select doesn't work for our purposes.
#if sys.version_info >= (2,6):
#    raise ValueError("select_backport is not required in Python 2.6+")

extensions = [
    Extension("select_backport", ["select_backportmodule.c"],
        define_macros = MACROS,
        )
    ]

setup(
    name = "select_backport",
    version = "0.2",
    description = __doc__[:__doc__.find('\n')].strip(),
    long_description = '\n'.join([line
                                  for line in __doc__.split('\n')[1:]]),
    author = "Christian Heimes",
    author_email = "christian@cheimes.de",
    maintainer = "Gora Khargosh",
    maintainer_email = "gora.khargosh@gmail.com",
    download_url = "http://pypi.python.org/",
    license = "MIT",
    keywords = "select poll epoll kqueue",
    ext_modules = extensions,
    packages = ["tests"],
    include_package_data = True,
    platforms = ["Linux 2.6", "BSD", "Mac OS X"],
    provides = ["select_backport"],
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

