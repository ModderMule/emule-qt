#!/usr/bin/env python3
"""Symbolize Windows stall/crash addresses with a PDB, stdlib only (no llvm-pdbutil needed).

Usage: pdbsym.py <file.pdb> <addr> [<addr> ...]
  addr: RVA in hex, as "12fa783", "0x12fa783" or "emulecored.exe+0x12fa783"

Getting the PDB: release.yml's main-branch run (not the tag run) uploads artifact
debug-symbols-win64-v<ver> (kept 90 days):
  gh run list -w release.yml -b main
  gh run download <run-id> -n debug-symbols-win64-v<ver> -D /tmp/sym

Reads public symbols (S_PUB32) from the symbol record stream and procedure
symbols (S_GPROC32 / S_LPROC32) from every module stream (MSF 7.0).
"""
import bisect
import struct
import sys


def read_msf(path):
    data = open(path, 'rb').read()
    magic = b'Microsoft C/C++ MSF 7.00\r\n\x1aDS\0\0\0'
    assert data[:len(magic)] == magic, 'not an MSF 7.0 file'
    block_size, _fpm, num_blocks, dir_size, _unk, block_map_addr = struct.unpack_from('<6I', data, 32)

    def block(i):
        return data[i * block_size:(i + 1) * block_size]

    dir_blocks_n = (dir_size + block_size - 1) // block_size
    dir_block_ids = struct.unpack_from('<%dI' % dir_blocks_n, block(block_map_addr))
    directory = b''.join(block(i) for i in dir_block_ids)[:dir_size]
    num_streams = struct.unpack_from('<I', directory, 0)[0]
    sizes = struct.unpack_from('<%dI' % num_streams, directory, 4)
    pos = 4 + 4 * num_streams
    streams = []
    for size in sizes:
        if size == 0xFFFFFFFF:
            streams.append(b'')
            continue
        n = (size + block_size - 1) // block_size
        ids = struct.unpack_from('<%dI' % n, directory, pos)
        pos += 4 * n
        streams.append(b''.join(block(i) for i in ids)[:size])
    return streams


def parse_records(buf, start, end, out, sections):
    pos = start
    while pos + 4 <= end:
        reclen, kind = struct.unpack_from('<HH', buf, pos)
        if reclen < 2:
            break
        body = pos + 4
        if kind == 0x110E:  # S_PUB32
            _flags, off, seg = struct.unpack_from('<IIH', buf, body)
            name = buf[body + 10:buf.index(b'\0', body + 10)]
            add(out, sections, seg, off, 0, name, 'pub')
        elif kind in (0x1110, 0x110F, 0x1146, 0x1147):  # S_GPROC32/S_LPROC32(+_ID)
            _parent, _end, _next, length, _dbs, _dbe, _type, off, seg, _flags = struct.unpack_from(
                '<IIIIIIIIHB', buf, body)
            name = buf[body + 35:buf.index(b'\0', body + 35)]
            add(out, sections, seg, off, length, name, 'proc')
        pos += 2 + reclen


def add(out, sections, seg, off, length, name, kind):
    if 1 <= seg <= len(sections):
        out.append((sections[seg - 1] + off, length, name.decode('utf-8', 'replace'), kind))


def parse_addr(arg):
    if '+' in arg:
        arg = arg.rsplit('+', 1)[1]
    return int(arg, 16)


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    streams = read_msf(sys.argv[1])
    dbi = streams[3]
    (_sig, _ver, _age, _gsi, _bld, _psi, _pdbver, sym_rec, _rbld, mod_size, sec_contrib, sec_map,
     src_info, tsm_size, _mfc, dbg_size, ec_size, _flags, _machine, _pad) = struct.unpack_from(
        '<iIIHHHHHHiiiiiIiiHHI', dbi, 0)
    hdr = 64
    dbg_off = hdr + mod_size + sec_contrib + sec_map + src_info + tsm_size + ec_size
    dbg_streams = struct.unpack_from('<%dH' % (dbg_size // 2), dbi, dbg_off)
    sec_hdr_stream = dbg_streams[5]
    sh = streams[sec_hdr_stream]
    sections = [struct.unpack_from('<I', sh, i * 40 + 12)[0] for i in range(len(sh) // 40)]

    syms = []
    sr = streams[sym_rec]
    parse_records(sr, 0, len(sr), syms, sections)

    # module streams: DBI module info substream
    pos = hdr
    end = hdr + mod_size
    while pos < end:
        mod_stream, sym_bytes = struct.unpack_from('<HxxI', dbi, pos + 34)
        p = pos + 64
        p = dbi.index(b'\0', p) + 1   # module name
        p = dbi.index(b'\0', p) + 1   # object name
        pos = (p + 3) & ~3
        if mod_stream != 0xFFFF and mod_stream < len(streams):
            ms = streams[mod_stream]
            parse_records(ms, 4, min(sym_bytes, len(ms)), syms, sections)

    syms.sort()
    addrs = [s[0] for s in syms]
    for arg in sys.argv[2:]:
        rva = parse_addr(arg)
        i = bisect.bisect_right(addrs, rva) - 1
        # prefer a proc that covers the address
        best = None
        j = i
        while j >= 0 and rva - syms[j][0] < 0x100000:
            a, length, name, kind = syms[j]
            if kind == 'proc' and a <= rva < a + length:
                best = syms[j]
                break
            j -= 1
        if best is None and i >= 0:
            best = syms[i]
        if best:
            print('%s -> %s+0x%x (%s)' % (arg, best[2], rva - best[0], best[3]))
        else:
            print('%s -> ?' % arg)


if __name__ == '__main__':
    main()
