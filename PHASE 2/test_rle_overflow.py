#!/usr/bin/env python3

"""
RLE Integer Overflow Test Generator

Generates BUN files to test vulnerability F-02:
RLE Uncompressed Size Integer Overflow

Vulnerability: The parser accumulates RLE pair counts without overflow detection.
Location: bun_parse.c, lines 339-345
Code: u64 total_uncompressed_size += rle_pair[0]; (no overflow check)

This script generates multiple test cases to explore RLE integer overflow:
1. Normal RLE file (baseline - should pass)
2. RLE size mismatch (declared size != sum of pair counts)
3. RLE with potential overflow (many large pair counts)
4. Edge cases (max values, boundary conditions)

Usage:
    python3 test_rle_overflow.py [--verbose] [--output-dir DIR]

Output:
    test_rle_normal.bun              - Valid RLE file (baseline)
    test_rle_size_mismatch.bun       - RLE pairs don't match uncompressed_size
    test_rle_large_counts.bun        - Many large RLE pair counts
    test_rle_edge_case_max.bun       - Edge case with maximum values
"""

import struct
import sys
from pathlib import Path
from typing import List, Tuple
import argparse

# Constants from spec
BUN_MAGIC = 0x304E5542
BUN_VERSION_MAJOR = 1
BUN_VERSION_MINOR = 0

COMPRESS_NONE = 0
COMPRESS_RLE = 1
COMPRESS_ZLIB = 2

FLAG_ENCRYPTED = 0x1
FLAG_EXECUTABLE = 0x2

# Format strings
_HEADER_FMT = "<IHHIQQQQQQ"
_RECORD_FMT = "<IIQQQIIII"

HEADER_SIZE = 60
RECORD_SIZE = 48

def align4(n: int) -> int:
    """Round n up to the next multiple of 4."""
    return (n + 3) & ~3

def write_header(f, *, asset_count, asset_table_offset, string_table_offset,
                 string_table_size, data_section_offset, data_section_size,
                 magic=BUN_MAGIC, version_major=BUN_VERSION_MAJOR,
                 version_minor=BUN_VERSION_MINOR, reserved=0):
    """Write BUN header to file."""
    data = struct.pack(
        _HEADER_FMT,
        magic,
        version_major,
        version_minor,
        asset_count,
        asset_table_offset,
        string_table_offset,
        string_table_size,
        data_section_offset,
        data_section_size,
        reserved,
    )
    f.write(data)

def write_asset_record(f, *, name_offset, name_length, data_offset, data_size,
                       uncompressed_size=0, compression=COMPRESS_NONE,
                       asset_type=0, checksum=0, flags=0):
    """Write BUN asset record to file."""
    data = struct.pack(
        _RECORD_FMT,
        name_offset,
        name_length,
        data_offset,
        data_size,
        uncompressed_size,
        compression,
        asset_type,
        checksum,
        flags,
    )
    f.write(data)

def create_rle_test_file(output_path: Path, test_name: str, 
                        rle_pairs: List[Tuple[int, int]], 
                        declared_uncompressed_size: int = None,
                        verbose: bool = False) -> None:
    """
    Create a BUN file with RLE-compressed data.
    
    Args:
        output_path: Path to write the BUN file
        test_name: Description of the test
        rle_pairs: List of (count, value) tuples for RLE data
        declared_uncompressed_size: If None, calculated from pairs; 
                                   if specified, use that value instead (for mismatch tests)
        verbose: Print detailed information
    """
    
    asset_name = b"rle_test"
    asset_count = 1
    
    # Calculate actual uncompressed size from pairs
    actual_uncompressed_size = sum(count for count, _ in rle_pairs)
    
    # Use declared size if provided, otherwise use actual
    uncompressed_size = declared_uncompressed_size if declared_uncompressed_size is not None else actual_uncompressed_size
    
    # Create RLE data (count, value pairs)
    rle_data = b""
    for count, value in rle_pairs:
        rle_data += bytes([count, value])
    
    # Compute offsets
    asset_table_offset = align4(HEADER_SIZE)
    string_table_offset = align4(asset_table_offset + asset_count * RECORD_SIZE)
    string_table_size = align4(len(asset_name))
    data_section_offset = align4(string_table_offset + string_table_size)
    data_section_size = align4(len(rle_data))
    
    if verbose:
        print(f"\n{'='*70}")
        print(f"Creating: {output_path.name}")
        print(f"Test: {test_name}")
        print(f"{'='*70}")
        print(f"Asset name: {asset_name}")
        print(f"RLE pairs: {len(rle_pairs)} pairs")
        print(f"RLE data size: {len(rle_data)} bytes")
        print(f"Actual uncompressed size (sum of counts): {actual_uncompressed_size}")
        print(f"Declared uncompressed_size field: {uncompressed_size}")
        if declared_uncompressed_size is not None and declared_uncompressed_size != actual_uncompressed_size:
            print(f"⚠️  MISMATCH: Declared ({uncompressed_size}) != Actual ({actual_uncompressed_size})")
        print(f"\nOffsets and sizes:")
        print(f"  asset_table_offset:  {asset_table_offset}")
        print(f"  string_table_offset: {string_table_offset}")
        print(f"  string_table_size:   {string_table_size}")
        print(f"  data_section_offset: {data_section_offset}")
        print(f"  data_section_size:   {data_section_size}")
    
    # Write file
    with open(output_path, "wb") as f:
        write_header(
            f,
            asset_count=asset_count,
            asset_table_offset=asset_table_offset,
            string_table_offset=string_table_offset,
            string_table_size=string_table_size,
            data_section_offset=data_section_offset,
            data_section_size=data_section_size,
        )
        
        # Padding before asset table
        padding = asset_table_offset - HEADER_SIZE
        if padding > 0:
            f.write(b"\x00" * padding)
        
        # Asset record
        write_asset_record(
            f,
            name_offset=0,
            name_length=len(asset_name),
            data_offset=0,
            data_size=len(rle_data),
            uncompressed_size=uncompressed_size,
            compression=COMPRESS_RLE,
        )
        
        # Padding before string table
        padding = string_table_offset - (asset_table_offset + RECORD_SIZE)
        if padding > 0:
            f.write(b"\x00" * padding)
        
        # String table
        f.write(asset_name)
        padding = string_table_size - len(asset_name)
        if padding > 0:
            f.write(b"\x00" * padding)
        
        # Padding before data section
        padding = data_section_offset - (string_table_offset + string_table_size)
        if padding > 0:
            f.write(b"\x00" * padding)
        
        # RLE data
        f.write(rle_data)
        padding = data_section_size - len(rle_data)
        if padding > 0:
            f.write(b"\x00" * padding)
    
    file_size = output_path.stat().st_size
    if verbose:
        print(f"✓ Wrote {output_path.name} ({file_size} bytes)\n")
    else:
        print(f"✓ {output_path.name:<40} ({file_size:>8} bytes)")

def generate_test_suite(output_dir: Path, verbose: bool = False) -> None:
    """Generate complete RLE overflow test suite."""
    
    output_dir.mkdir(parents=True, exist_ok=True)
    
    print(f"\nGenerating RLE Integer Overflow Test Suite")
    print(f"Output directory: {output_dir}\n")
    
    # Test 1: Normal RLE file (baseline)
    # Should pass: pairs sum to 255,000, declared = 255,000
    rle_pairs_normal = [(255, 0x41) for _ in range(1000)]  # 1000 pairs, each count=255
    create_rle_test_file(
        output_dir / "test_rle_normal.bun",
        "Normal RLE file - baseline (should pass)",
        rle_pairs_normal,
        verbose=verbose
    )
    
    # Test 2: Size mismatch - declared > actual
    # Should fail: pairs sum to 255,000, declared = 300,000
    create_rle_test_file(
        output_dir / "test_rle_size_mismatch_too_high.bun",
        "RLE size mismatch - declared size > actual sum",
        rle_pairs_normal,
        declared_uncompressed_size=300_000,  # Mismatch!
        verbose=verbose
    )
    
    # Test 3: Size mismatch - declared < actual
    # Should fail: pairs sum to 255,000, declared = 100,000
    create_rle_test_file(
        output_dir / "test_rle_size_mismatch_too_low.bun",
        "RLE size mismatch - declared size < actual sum",
        rle_pairs_normal,
        declared_uncompressed_size=100_000,  # Mismatch!
        verbose=verbose
    )
    
    # Test 4: Large RLE counts (many pairs with count=255)
    # Each pair: count=255, value varies
    # 5000 pairs × 255 = 1,275,000 bytes uncompressed
    rle_pairs_large = [(255, (i % 256)) for i in range(5000)]
    create_rle_test_file(
        output_dir / "test_rle_large_counts.bun",
        "RLE with large counts (5000 pairs × 255 each)",
        rle_pairs_large,
        verbose=verbose
    )
    
    # Test 5: Edge case - maximum u8 count value
    # 100 pairs with count=255 (max u8)
    rle_pairs_max = [(255, 0x42) for _ in range(100)]
    create_rle_test_file(
        output_dir / "test_rle_max_count.bun",
        "RLE with maximum count values (255)",
        rle_pairs_max,
        verbose=verbose
    )
    
    # Test 6: Edge case - minimum count value (1)
    # Many pairs with count=1
    rle_pairs_min = [(1, 0x43) for _ in range(1000)]
    create_rle_test_file(
        output_dir / "test_rle_min_count.bun",
        "RLE with minimum count values (1)",
        rle_pairs_min,
        verbose=verbose
    )
    
    # Test 7: Edge case - mixed counts
    # Variable counts to test accumulation
    rle_pairs_mixed = []
    for i in range(100):
        count = (i % 255) + 1  # 1 to 255
        rle_pairs_mixed.append((count, (i % 256)))
    create_rle_test_file(
        output_dir / "test_rle_mixed_counts.bun",
        "RLE with mixed count values (1-255)",
        rle_pairs_mixed,
        verbose=verbose
    )
    
    # Test 8: Zero in uncompressed_size (declared 0, but has data)
    # Spec: if compression=1 (RLE), uncompressed_size must be > 0
    # But let's test what happens
    create_rle_test_file(
        output_dir / "test_rle_zero_uncompressed.bun",
        "RLE with declared uncompressed_size = 0 (spec violation)",
        [(255, 0x44) for _ in range(10)],
        declared_uncompressed_size=0,
        verbose=verbose
    )
    
    # Test 9: Very small RLE data
    # Single pair, count=5
    create_rle_test_file(
        output_dir / "test_rle_single_pair.bun",
        "RLE with single pair (count=5)",
        [(5, 0x45)],
        verbose=verbose
    )
    
    # Test 10: Odd number of bytes (spec says must be even)
    # Create a file with odd-length RLE data (this will fail the "must be divisible by 2" check)
    # Actually, we can't easily create this without breaking the pairing, so skip it
    # The generator naturally creates even-length data
    
    print(f"\n{'='*70}")
    print(f"Test suite generation complete!")
    print(f"{'='*70}\n")

def print_usage() -> None:
    """Print usage information."""
    print("""
RLE Integer Overflow Test Generator

Usage:
    python3 test_rle_overflow.py [OPTIONS]

Options:
    --verbose, -v       Print detailed information for each test
    --output-dir DIR    Output directory (default: current directory)
    --help, -h          Show this help message

Examples:
    # Generate tests with verbose output
    python3 test_rle_overflow.py --verbose

    # Generate tests to specific directory
    python3 test_rle_overflow.py --output-dir tests/fixtures/invalid/

    # Generate tests with both options
    python3 test_rle_overflow.py -v --output-dir ./rle_tests

Test Files Generated:
    1. test_rle_normal.bun
       - Valid RLE file (baseline - should pass)
       - 1000 pairs, 255,000 bytes uncompressed

    2. test_rle_size_mismatch_too_high.bun
       - Declared size (300,000) > actual sum (255,000)
       - Should trigger mismatch error

    3. test_rle_size_mismatch_too_low.bun
       - Declared size (100,000) < actual sum (255,000)
       - Should trigger mismatch error

    4. test_rle_large_counts.bun
       - 5000 pairs with count=255 each
       - 1,275,000 bytes uncompressed

    5. test_rle_max_count.bun
       - 100 pairs with maximum count (255)

    6. test_rle_min_count.bun
       - 1000 pairs with minimum count (1)

    7. test_rle_mixed_counts.bun
       - Mixed counts (1-255) for accumulation testing

    8. test_rle_zero_uncompressed.bun
       - Declared uncompressed_size = 0 with RLE data
       - Violates spec requirement

    9. test_rle_single_pair.bun
       - Single RLE pair (count=5)
       - Minimal test case

Vulnerability Details:
    Location: bun_parse.c, lines 339-345
    Code: u64 total_uncompressed_size += rle_pair[0];
    Issue: No overflow detection during accumulation
    
    The parser sums RLE pair counts without checking for overflow.
    If the sum exceeds UINT64_MAX, the value wraps around modulo 2^64.
    This could allow an attacker to bypass validation by crafting
    pairs whose sum wraps to match the declared uncompressed_size.
    """)

def main():
    """Main entry point."""
    parser = argparse.ArgumentParser(
        description="Generate RLE integer overflow test files",
        add_help=False  # We handle help manually
    )
    parser.add_argument('-v', '--verbose', action='store_true',
                        help='Print detailed information')
    parser.add_argument('--output-dir', type=Path, default=Path('.'),
                        help='Output directory (default: current directory)')
    parser.add_argument('-h', '--help', action='store_true',
                        help='Show help message')
    
    args = parser.parse_args()
    
    if args.help:
        print_usage()
        return 0
    
    try:
        generate_test_suite(args.output_dir, verbose=args.verbose)
        return 0
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        return 1

if __name__ == "__main__":
    sys.exit(main())
