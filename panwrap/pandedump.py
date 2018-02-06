#!/usr/bin/python3

import argparse
import re
import enum
import sys
import mmap
import itertools
from pathlib import *
from functools import partial

class PanwrapDump:
    class ProtectionFlag(enum.IntFlag):
        EXEC = mmap.PROT_EXEC
        READ = mmap.PROT_READ
        WRITE = mmap.PROT_WRITE

    class GPUMemFlag(enum.IntFlag):
        # IN
        """ Read access CPU side """
        PROT_CPU_RD = (1 << 0)
        """ Write access CPU side """
        PROT_CPU_WR = (1 << 1)
        """ Read access GPU side """
        PROT_GPU_RD = (1 << 2)
        """ Write access GPU side """
        PROT_GPU_WR = (1 << 3)
        """ Execute allowed on the GPU side """
        PROT_GPU_EX = (1 << 4)
        """ Grow backing store on GPU Page Fault """
        GROW_ON_GPF = (1 << 9)

        """ Page coherence Outer shareable, if available """
        COHERENT_SYSTEM = (1 << 10)
        """ Page coherence Inner shareable """
        COHERENT_LOCAL = (1 << 11)
        """ Should be cached on the CPU """
        CACHED_CPU = (1 << 12)

        # IN/OUT
        """ Must have same VA on both the GPU and the CPU """
        SAME_VA = (1 << 13)

        # OUT
        """ Must call mmap to acquire a GPU address for the alloc """
        NEED_MMAP = (1 << 14)

        # IN
        """ Page coherence Outer shareable, required. """
        COHERENT_SYSTEM_REQUIRED = (1 << 15)
        """ Secure memory """
        SECURE = (1 << 16)
        """ Not needed physical memory """
        DONT_NEED = (1 << 17)
        """
        Must use shared CPU/GPU zone (SAME_VA zone) but doesn't require the
        addresses to be the same
        """
        IMPORT_SHARED = (1 << 18)

    class Entry:
        __slots__ = ['sec', 'nsec', 'ioctl', 'mem_profile', 'atoms',
                     'mmap_table']

        NAME_PATTERN = r'([0-9]+)\.([0-9]+)-([A-Z_]+)'

        def __init__(self, path):
            self.sec, self.nsec, self.ioctl = \
                    re.match(self.NAME_PATTERN, path.name).groups()
            self.atoms = (path / 'atoms').read_text()
            self.mmap_table = self.MmapTable(path / 'mmap_table')

            mem_profile = (path / 'mem_profile')
            if mem_profile.exists():
                self.mem_profile = mem_profile.read_text()
            else:
                self.mem_profile = None

        class MmapTable:
            class Entry:
                __slots__ = ['gpu_va', 'cpu_va', 'length', 'protection',
                             'flags']

                def __init__(self, mmap_str):
                    mmap_fields = mmap_str.split()
                    self.gpu_va = int(mmap_fields.pop(0), 16)
                    self.cpu_va = int(mmap_fields.pop(0), 16)
                    self.length = int(mmap_fields.pop(0), 16)
                    self.protection = PanwrapDump.ProtectionFlag(int(mmap_fields.pop(0), 16))
                    self.flags = PanwrapDump.GPUMemFlag(int(mmap_fields.pop(0), 16))

                def __str__(self):
                    return "0x%x-0x%x @ 0x%x (length=%d prot=0x%x flags=0x%x)" % (
                        self.gpu_va, self.gpu_va + self.length - 1,
                        self.cpu_va, self.length, self.protection, self.flags)

                def contains_gpu(self, gpu_va):
                    return (self.gpu_va <= ptr and
                            (self.gpu_va + self.length - 1) >= ptr)

                def contains_cpu(self, cpu_va):
                    return (self.cpu_va <= ptr and
                            (self.cpu_va + self.length - 1) >= ptr)

                class Page:
                    __slots__ = ['start_addr', 'contents']

                    # Debugfs page format looks like this:
                    #
                    # 0000007237000000: 00000000 00000000 00000000 00000000
                    # 0000007237000010: 00000000 00000000 00000000 00000000
                    # 0000007237000020: 00000000 00000000 00000000 00000000
                    # …                 offsets: (+3, +2, +1, +0) ---^
                    # 0000007237000ff0: 00000000 00000000 00000000 00000000
                    # <empty-line> ← marks the end of the page
                    #
                    # Additionally, pages without backed memory look like this:
                    #
                    # 0000007237001000: Unbacked page
                    # <empty-line> ← marks the end of the page

                    def __init__(self, page_str):
                        lines = page_str.split('\n')
                        self.start_addr, start_contents = lines[0].split(': ')
                        self.start_addr = int(self.start_addr, 16)

                        if start_contents.strip() == 'Unbacked page':
                            self.contents = None

                        page_size = len(lines) * 16;
                        self.contents = bytearray(page_size)
                        for l in lines:
                            l = l.strip()
                            addr, contents = l.split(': ')
                            addr = int(addr, 16)

                            contents = contents.strip()

                            start_offset = addr - self.start_addr
                            byte_strs = [iter(contents.replace(' ', ''))]

                    def __len__(self):
                        return len(self.contents)

                    # if lines[0].split(': ')[1].strip() == 'Unbacked page':

            def __init__(self, mmap_table, mem_view):
                lines = mmap_table.read_text().strip().split('\n')
                self.__entries = [self.Entry(l) for l in lines]

                page_dumps = [self.Entry.Page(s) for s in
                              mem_view.read_text().strip().split('\n\n')]


            def find_handle_gpu(self, ptr):
                """
                Find information on the mapped memory handle containing gpu
                pointer ptr
                """
                for e in self.__entries:
                    if e.contains_gpu(ptr):
                        return e

            def find_handle_cpu(self, ptr):
                """
                Find information on the mapped memory handle containing cpu
                pointer ptr
                """
                for e in self.__entries:
                    if e.contains_cpu(ptr):
                        return e

            @property
            def entries(self):
                return self.__entries

    def __init__(self, path):
        self.path = path
        self.entries = []
        for subdir in path.iterdir():
            p = Path(subdir)
            if not re.match(self.Entry.NAME_PATTERN, p.name):
                continue

            self.entries.append(self.Entry(p))

def parse_dir_arg(arg):
    try:
        pdir = Path(arg)
        if not pdir.is_dir():
            raise Exception("%s isn't a directory" % arg)
    except Exception as e:
        raise argparse.ArgumentTypeError(e.args)

    return pdir

parser = argparse.ArgumentParser(description='Decode panwrap memory dumps')
parser.add_argument('dump_dir', metavar='DIR',
                    help='The dump directory to view',
                    type=parse_dir_arg)
args = parser.parse_args()

dump = PanwrapDump(args.dump_dir)
print(len(dump.entries))
for e in dump.entries:
    print(list(str(f) for f in e.mmap_table))
