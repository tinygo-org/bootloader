package main

import (
	"debug/elf"
	"fmt"
	"io/ioutil"
	"os"
	"sort"
)

type progSlice []*elf.Prog

func (s progSlice) Len() int           { return len(s) }
func (s progSlice) Less(i, j int) bool { return s[i].Paddr < s[j].Paddr }
func (s progSlice) Swap(i, j int)      { s[i], s[j] = s[j], s[i] }

// extractELF extracts a firmware image and the first load address from the
// given ELF file. It tries to emulate the behavior of objcopy.
func extractELF(fp *os.File) (uint64, []byte, error) {
	f, err := elf.NewFile(fp)
	if err != nil {
		return 0, nil, fmt.Errorf("failed to open ELF file to extract text segment: %w", err)
	}
	defer f.Close()

	// The GNU objcopy command does the following for firmware extraction (from
	// the man page):
	// > When objcopy generates a raw binary file, it will essentially produce a
	// > memory dump of the contents of the input object file. All symbols and
	// > relocation information will be discarded. The memory dump will start at
	// > the load address of the lowest section copied into the output file.

	// Find the lowest section address.
	startAddr := ^uint64(0)
	for _, section := range f.Sections {
		if section.Type != elf.SHT_PROGBITS || section.Flags&elf.SHF_ALLOC == 0 {
			continue
		}
		if section.Addr < startAddr {
			startAddr = section.Addr
		}
	}

	progs := make(progSlice, 0, 2)
	for _, prog := range f.Progs {
		if prog.Type != elf.PT_LOAD || prog.Filesz == 0 {
			continue
		}
		progs = append(progs, prog)
	}
	if len(progs) == 0 {
		return 0, nil, fmt.Errorf("file does not contain ROM segments")
	}
	sort.Sort(progs)

	var rom []byte
	for _, prog := range progs {
		if prog.Paddr != progs[0].Paddr+uint64(len(rom)) {
			return 0, nil, fmt.Errorf("ROM segments are non-contiguous")
		}
		data, err := ioutil.ReadAll(prog.Open())
		if err != nil {
			return 0, nil, fmt.Errorf("failed to extract segment from ELF file")
		}
		rom = append(rom, data...)
	}
	if progs[0].Paddr < startAddr {
		// The lowest memory address is before the first section. This means
		// that there is some extra data loaded at the start of the image that
		// should be discarded.
		// Example: ELF files where .text doesn't start at address 0 because
		// there is a bootloader at the start.
		return startAddr, rom[startAddr-progs[0].Paddr:], nil
	} else {
		return progs[0].Paddr, rom, nil
	}
}

func readInput(filename string) (uint64, []byte, error) {
	f, err := os.Open(filename)
	if err != nil {
		return 0, nil, err
	}
	defer f.Close()

	// Read the magic (first 4 bytes) of the file.
	magic := make([]byte, 4)
	_, err = f.ReadAt(magic, 0)
	if err != nil {
		return 0, nil, err
	}

	// Determine file type, and extract content.
	switch {
	case string(magic) == "\x7fELF":
		return extractELF(f)
	default:
		return 0, nil, fmt.Errorf("could not determine file type (magic: %02x %02x %02x %02x)", magic[0], magic[1], magic[2], magic[3])
	}
}
