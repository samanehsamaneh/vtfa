# pylint: disable=no-name-in-module,import-error
import os
import urllib2
import subprocess
import sys
import shutil
import glob
import tarfile
import multiprocessing
import platform

try:
    from setuptools import setup
    from setuptools import find_packages
    packages = find_packages()
except ImportError:
    from distutils.core import setup
    packages = [x.strip('./').replace('/','.') for x in os.popen('find -name "__init__.py" | xargs -n1 dirname').read().strip().split('\n')]

from distutils.util import get_platform
from distutils.errors import LibError
from distutils.command.build import build as _build

if sys.platform in ('win32', 'cygwin'):
    library_file = 'pyvex.dll'
elif sys.platform == 'darwin':
    library_file = "libpyvex.dylib"
else:
    library_file = "libpyvex.so"


VEX_LIB_NAME = "vex" # can also be vex-amd64-linux
VEX_PATH = "vex"
VEX_PATH = '../vex'
if not os.path.exists(VEX_PATH):
    VEX_URL = 'https://github.com/angr/vex/archive/master.tar.gz'
    with open('master.tar.gz', 'wb') as v:
        v.write(urllib2.urlopen(VEX_URL).read())
    with tarfile.open('master.tar.gz') as tar:
        tar.extractall()
    VEX_PATH='./vex-master'

def _build_vex():
    cmd = ['nmake', '/f', 'Makefile-win', 'all'] if sys.platform == 'win32' else ['make', '-j', str(multiprocessing.cpu_count())]
    if subprocess.call(cmd, cwd=VEX_PATH) != 0:
        raise LibError("Unable to build libVEX.")

def _build_pyvex():
    e = os.environ.copy()
    e['VEX_PATH'] = os.path.join('..', VEX_PATH)
    cmd = ['cl', '-LD', '-O2' ,'-I' + os.path.join('..', VEX_PATH, 'pub'), 'pyvex.c', 'logging.c', os.path.join('..', VEX_PATH, 'libvex.lib'), '/link', '/DEF:pyvex.def'] if sys.platform == 'win32' else ['make', '-j', str(multiprocessing.cpu_count())]
    if subprocess.call(cmd, cwd='pyvex_c', env=e) != 0:
        raise LibError("Unable to build libpyvex.")

def _shuffle_files():
    shutil.rmtree('pyvex/lib', ignore_errors=True)
    shutil.rmtree('pyvex/include', ignore_errors=True)
    os.mkdir('pyvex/lib')
    os.mkdir('pyvex/include')

    shutil.copy(os.path.join('pyvex_c', library_file), 'pyvex/lib')
    shutil.copy('pyvex_c/pyvex.h', 'pyvex/include')
    for f in glob.glob(os.path.join(VEX_PATH, 'pub', '*')):
        shutil.copy(f, 'pyvex/include')

def _build_ffi():
    import make_ffi
    try:
        make_ffi.doit(os.path.join(VEX_PATH,'pub'))
    except Exception as e:
        print repr(e)
        raise

class build(_build):
    def run(self):
        self.execute(_build_vex, (), msg="Building libVEX")
        self.execute(_build_pyvex, (), msg="Building pyvex-static")
        self.execute(_shuffle_files, (), msg="Copying libraries and headers")
        self.execute(_build_ffi, (), msg="Creating CFFI defs file")
        _build.run(self)
cmdclass = { 'build': build }

try:
    from setuptools.command.develop import develop as _develop
    from setuptools.command.bdist_egg import bdist_egg as _bdist_egg
    class develop(_develop):
        def run(self):
            self.execute(_build_vex, (), msg="Building libVEX")
            self.execute(_build_pyvex, (), msg="Building pyvex-static")
            self.execute(_shuffle_files, (), msg="Copying libraries and headers")
            self.execute(_build_ffi, (), msg="Creating CFFI defs file")
            _develop.run(self)
    cmdclass['develop'] = develop

    class bdist_egg(_bdist_egg):
        def run(self):
            self.run_command('build')
            _bdist_egg.run(self)
    cmdclass['bdist_egg'] = bdist_egg
except ImportError:
    print "Proper 'develop' support unavailable."

if 'bdist_wheel' in sys.argv and '--plat-name' not in sys.argv:
    sys.argv.append('--plat-name')
    name = get_platform()
    if 'linux' in name:
        # linux_* platform tags are disallowed because the python ecosystem is fubar
        # linux builds should be built in the centos 5 vm for maximum compatibility
        sys.argv.append('manylinux1_' + platform.machine())
    else:
        # https://www.python.org/dev/peps/pep-0425/
        sys.argv.append(name.replace('.', '_').replace('-', '_'))

setup(
    name="pyvex", version='5.6.12.3', description="A Python interface to libVEX and VEX IR.",
    packages=['pyvex'],
    cmdclass=cmdclass,
    install_requires=[ 'pycparser', 'cffi>=1.0.3', 'archinfo' ],
    setup_requires=[ 'pycparser', 'cffi>=1.0.3' ],
    include_package_data=True,
    package_data={
        'pyvex': ['lib/*', 'include/*']
    }
)
