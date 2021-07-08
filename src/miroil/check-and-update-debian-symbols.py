#! /usr/bin/python3
"""This script checks that the symbols in a library match the given debian symbols, and
updates the debian symbols if needed.

USAGE: ./check-and-update-debian-symbols.py LIB_DIR_PATH LIB_NAME LIB_VERSION ABI_VERSION

To use: Go to your build folder and run "make check-miroil-symbols"""

import sys
from sys import stderr
import os
from os import path
import subprocess
import re

HELPTEXT = __doc__

def get_output_symbols_path():
    return '/tmp/temp_mir_debian_symbols.symbols'

class Context:
    def __init__(self, args):
        assert isinstance(args, list)
        i = 0
        arg_len = 5
        if len(args) == 0 or '-h' in args or '--help' in args:
            raise RuntimeError('help')
        assert len(args) == arg_len, 'There should be exactly ' + str(arg_len) + ' arguments, not ' + str(len(args))
        self.library_dir_path = args[i]
        i += 1
        self.library_name = args[i]
        i += 1
        self.library_version = [int(i) for i in args[i].split('.')]
        i += 1
        self.abi_version = int(args[i])
        i += 1
        self.abi_minor_version = int(args[i])
        i += 1
        assert i == arg_len, 'i does not match arg_len'
        self.library_so_path = self.library_dir_path + '/lib' + self.library_name + '.so.' + str(self.abi_version)
        self.deb_package_name = 'lib' + self.library_name + str(self.abi_version)
        self.deb_symbols_path = 'debian/' + self.deb_package_name + '.symbols'
        self.output_symbols_path = get_output_symbols_path()

    def validate(self):
        assert path.isdir(self.library_dir_path), 'library dir path (' + self.library_dir_path + ') is not a valid directory'
        assert not '/' in self.library_name, 'library name should be a file name only, not a path'
        assert not self.library_name.startswith('lib'), 'library name should just be the name with no lib prefix'
        assert len(self.library_version) == 3, 'library version should be made of 3 dot separated components'
        assert path.isfile(self.library_so_path), 'library .so path (' + self.library_so_path + ') is not a valid file'
        assert path.isfile(self.deb_symbols_path), 'debian symbols file (' + self.deb_symbols_path + ') does not exist'

class Run:
    def __init__(self, args, cwd=None, stdin=None):
        assert isinstance(args, list)
        assert isinstance(cwd, str) or cwd == None
        assert isinstance(stdin, str) or stdin == None
        self.args = args
        # print('Running `' + ' '.join(self.args) + '`')
        p = subprocess.Popen(
                args,
                cwd=cwd,
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE
            )
        stdin = bytes(stdin, 'utf-8') if stdin != None else None
        stdout, stderr = p.communicate(stdin)
        self.stdout = stdout.decode('utf-8') if stdout != None else ''
        self.stderr = stderr.decode('utf-8') if stderr != None else ''
        self.returncode = p.returncode

    def success(self):
        return self.returncode == 0

    def assert_success(self):
        assert self.success(), (
            'Command `' + ' '.join(self.args) + '` failed with return code ' + str(self.returncode) + ':\n' +
            '  OUT: ' + '\n       '.join(line for line in self.stdout.split('\n')) + '\n' +
            '  ERR: ' + '\n       '.join(line for line in self.stderr.split('\n')))

class SymbolsFile:
    def __init__(self, context):
        assert isinstance(context, Context)
        self.context = context
        f = open(context.deb_symbols_path, 'r')
        lines = f.readlines()
        f.close()
        self.three_component_version = (
            str(context.library_version[0]) + '.' +
            str(context.library_version[1]) + '.' +
            str(context.library_version[2]))
        self.version_marker_line = (
            ' ' + context.library_name.upper() + '_' +
            str(context.abi_version) + '.' + str(context.abi_minor_version) +
            '@' + context.library_name.upper() + '_' +
            str(context.abi_version) + '.' + str(context.abi_minor_version) +
            ' ' + self.three_component_version + '\n')
        for i in reversed(range(len(lines))):
            if lines[i] == self.version_marker_line:
                self.prev_version_lines = lines[:i]
                self.current_version_lines = lines[i+1:]
                return
        self.prev_version_lines = lines
        self.current_version_lines = []

    def write(self):
        f = open(context.deb_symbols_path, 'w')
        for line in self.prev_version_lines:
            f.write(line)
        f.write(self.version_marker_line)
        for line in self.current_version_lines:
            f.write(line)
        f.close()

def new_lines_from_unfiltered(unfiltered):
    '''From the unfiltered (mangled) output of dpkg-gensymbols, returns a usable patch'''
    result = Run(['c++filt'], stdin=unfiltered)
    result.assert_success()
    searched = re.findall('^\+ (.+::.+) ([\.\d]+)$', result.stdout, flags=re.MULTILINE)
    if searched:
        filtered = [' (c++)"' + symbol + '" ' + version + '\n' for symbol, version in searched]
        return list(set(filtered))
    else:
        return None

def check_symbols(context):
    '''Checks the symbols.
    Returns None if all is good, or the lines that need to get added to the debian symbols file otherwise'''
    assert isinstance(context, Context)
    if (path.isfile(context.output_symbols_path)):
        os.remove(context.output_symbols_path)
    args = [
        'dpkg-gensymbols',
        '-e' + context.library_so_path,
        '-p' + context.deb_package_name,
        '-v' + '.'.join(str(i) for i in context.library_version),
        '-O' + context.output_symbols_path,
        '-c4',
    ]
    result = Run(args)
    if result.success():
        return None
    else:
        symbols_to_add = new_lines_from_unfiltered(result.stdout)
        if symbols_to_add != None:
            return symbols_to_add
        else:
            result.assert_success() # Something's not right. This will fail with a useful error

def append_to_symbols_file(new_symbols, context):
    '''Append a string to the end of the debian symbols file'''
    assert isinstance(new_symbols, list)
    symbols_file = SymbolsFile(context)
    symbols_file.current_version_lines += new_symbols
    symbols_file.current_version_lines.sort()
    symbols_file.write()

if __name__ == '__main__':
    context = Context(sys.argv[1:])
    context.validate()
    symbols_to_add = check_symbols(context)
    if symbols_to_add != None:
        print('Adding symbols to ' + context.deb_symbols_path + ':\n' + ''.join(symbols_to_add))
        append_to_symbols_file(symbols_to_add, context)
        print('Debian symbols added')
