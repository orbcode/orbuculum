# Code from Maciej Nowak to test correct implementation in orbuculum

import os
from typing import Iterable
from elftools.elf.elffile import ELFFile
from elftools.dwarf.compileunit import CompileUnit, DIE
from elftools.dwarf.dwarfinfo import DWARFInfo
from elftools.dwarf.lineprogram import LineProgramEntry
import re
from dataclasses import dataclass


ALL_PCS = {}

@dataclass(frozen=True)
class PCRangeMapping:
    pc_start: int
    pc_end: int
    filename: str
    line: int
    column: int

PC_RANGES: list[PCRangeMapping] = []

def enumerate_functions_in_cu(cu: CompileUnit) -> Iterable[DIE]:
    for die in cu.iter_DIEs():
        if die.tag in ['DW_TAG_subprogram', 'DW_TAG_inlined_subroutine']:
            yield die

def safe_get_name(die: DIE) -> str:
    if 'DW_AT_name' in die.attributes:
        return die.attributes['DW_AT_name'].value.decode('utf-8')

    if die.tag == 'DW_TAG_inlined_subroutine':
        origin = die.get_DIE_from_attribute('DW_AT_abstract_origin')
        return safe_get_name(origin)

    assert False, f'No name for DIE {die.tag}'

def chop_into_sequences(line_entries: Iterable[LineProgramEntry]) -> Iterable[list[LineProgramEntry]]:
    current_sequence = []

    for entry in line_entries:
        current_sequence.append(entry)

        if entry.state is not None and entry.state.end_sequence:
            yield current_sequence
            current_sequence = []

    if len(current_sequence) > 0:
        yield current_sequence

def process_cu(cu: CompileUnit, dwarf: DWARFInfo):
    top_die = cu.get_top_DIE()
    path = top_die.get_full_path()

    line_db = dwarf.line_program_for_CU(cu)

    print(f'Compile unit: {path}')

    # if not 'main.c' in path:
    #     return

    print('\tLine entries')

    for sequence in chop_into_sequences(line_db.get_entries()):
        print(f'\t\tSequence: {len(sequence)} entries')
        last_entry = None
        for entry in sequence:
            if entry.state is None:
                continue

            if last_entry is None:
                last_entry = entry
                continue

            file_ref = last_entry.state.file
            file_container = line_db.header['file_entry'][file_ref - 1]

            if file_container.dir_index:
                directory = line_db.header['include_directory'][file_container.dir_index - 1].decode('utf-8')
                file = os.path.join(directory, file_container.name.decode('utf-8'))
                line = last_entry.state.line
                column = last_entry.state.column

                PC_RANGES.append(PCRangeMapping(
                    pc_start=last_entry.state.address,
                    pc_end=entry.state.address,
                    filename=file,
                    line=line,
                    column=column
            ))

        for r in PC_RANGES:
            print(r)

        last_entry = entry


    # for die in enumerate_functions_in_cu(cu):
    #     print('\t', die.tag, safe_get_name(die))


def main():
    elf = ELFFile(open("/home/dmarples/Develop/STM32F730/build/STM32F730.elf", 'rb'))

    dwarf = elf.get_dwarf_info()

    for cu in dwarf.iter_CUs():
        process_cu(cu, dwarf)

    print('Highlighting listing file')

    instr_pattern = re.compile(r"^ ([a-z0-9]+):.*$")

    with open("/home/dmarples/Develop/STM32F730/build/STM32F730.list", 'r') as f_in:
        with open("/home/dmarples/STM32F730/build/STM32F730_highlighted.list", 'w') as f_out:
            for line in f_in:
                instr = instr_pattern.match(line)
                if instr is None:
                    f_out.write('I' + line)
                    continue

                pc = int(instr.group(1), 16)
                matching_ranges = [x for x in PC_RANGES if x.pc_start <= pc < x.pc_end]
                if len(matching_ranges) == 0:
                    f_out.write('M' + line)
                    continue
                else:
                    assert len(matching_ranges) == 1
                    f_out.write('F' + line.replace('\t', '  ').rstrip().ljust(100) + f'File: {matching_ranges[0].filename}:{matching_ranges[0].line}:{matching_ranges[0].column}' + '\n')

main()
