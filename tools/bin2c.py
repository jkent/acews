#!/usr/bin/env python
# SPDX-License-Identifier: MIT
from argparse import ArgumentParser
import os


def save_array_c(src_path, dst_path, symbol):
    length = os.path.getsize(src_path)

    with open(src_path, 'rb') as src_f:
        with open(dst_path, 'w') as dst_f:
            dst_f.write('#include <stddef.h>\n')
            dst_f.write('#include <stdint.h>\n')
            dst_f.write('\n')
            dst_f.write(f'const size_t {symbol}_len = {length};\n')
            dst_f.write(f'const uint8_t {symbol}[] = {{\n')
            while True:
                data = src_f.read(12)
                if not data:
                    break
                data = [f'0x{byte:02X}' for byte in data]
                s = ', '.join(data)
                dst_f.write(f'    {s},\n')
            dst_f.write('};\n')

def save_asm_c(src_path, dst_path, symbol):
    length = os.path.getsize(src_path)

    with open(dst_path, 'w') as dst_f:
        dst_f.write('#include <stddef.h>\n')
        dst_f.write('#include <stdint.h>\n')
        dst_f.write('\n')
        dst_f.write('#if SIZE_MAX == UINT64_MAX\n')
        dst_f.write('# define BALIGN "8"\n')
        dst_f.write('# define TYPE ".quad"\n')
        dst_f.write('#else\n')
        dst_f.write('# define BALIGN "4"\n')
        dst_f.write('# define TYPE ".int"\n')
        dst_f.write('#endif\n')
        dst_f.write('\n')
        dst_f.write('asm (\n')
        dst_f.write('    ".section .rodata\\n"\n')
        dst_f.write('    ".balign " BALIGN "\\n"\n')
        dst_f.write(f'    ".global {symbol}_len\\n"\n')
        dst_f.write(f'    "{symbol}_len:\\n"\n')
        dst_f.write(f'    TYPE " {length}\\n"\n')
        dst_f.write(f'    ".global {symbol}\\n"\n')
        dst_f.write(f'    "{symbol}:\\n"\n')
        dst_f.write(f'    ".incbin \\"{src_path}\\"\\n"\n')
        dst_f.write('    ".balign " BALIGN "\\n"\n')
        dst_f.write('    ".section .text\\n"\n')
        dst_f.write(');\n')

if __name__ == '__main__':
    parser = ArgumentParser()
    parser.add_argument('--use-asm', action='store_true',
            help='use faster but non-portable inline asm method')
    parser.add_argument('binary', metavar='BINARY', help='source binary data')
    parser.add_argument('output', metavar='OUTPUT', help='destination C source')
    args = parser.parse_args()

    symbol = os.path.basename(args.binary).translate(str.maketrans('-.', '__'))
    fn = save_array_c if not args.use_asm else save_asm_c
    fn(args.binary, args.output, symbol)
