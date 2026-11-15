FUNCTIONS = ''

class HeaderError(Exception):
    pass

def names():
    return []

def generate(*args, **kwargs):
    raise HeaderError('the kit has no function table: sdk/op1fw.h is the header')

def write(*args, **kwargs):
    raise HeaderError('the kit has no function table: sdk/op1fw.h is the header')
