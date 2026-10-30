"""fwheader in the authors' kit: the firmware's functions come made, as sdk/op1fw.h, so there is no table here to
make it from; plugin.py then compiles against sdk/op1fw.h. The names build.py and plugin.py ask for, empty."""

FUNCTIONS = ''                             # no function table: os.path.isfile('') is False


class HeaderError(Exception):
    pass


def names():
    return []


def generate(*args, **kwargs):
    raise HeaderError('the kit has no function table: sdk/op1fw.h is the header')


def write(*args, **kwargs):
    raise HeaderError('the kit has no function table: sdk/op1fw.h is the header')
