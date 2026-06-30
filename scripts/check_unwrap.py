#!/usr/bin/env python3
"""Check for unwrap()/expect() in production (non-test) Rust code."""

import sys, re
from pathlib import Path

src = Path('/home/vlabe/Documents/ecotiter-firmware-rust/src')
errors = []

for rs_file in sorted(src.rglob('*.rs')):
    if rs_file.name == 'main.rs':
        continue
    text = rs_file.read_text()
    lines = text.split('\n')
    
    in_test_block = 0  # brace depth tracking for #[cfg(test)] blocks
    
    for i, line in enumerate(lines):
        stripped = line.strip()
        
        # Track #[cfg(test)] blocks
        if re.search(r'#\[cfg\(test\)\]', line):
            # Next non-empty/comment line should open a brace
            lookahead = i + 1
            while lookahead < len(lines) and (lines[lookahead].strip() == '' or lines[lookahead].strip().startswith('//')):
                lookahead += 1
            if lookahead < len(lines) and '{' in lines[lookahead]:
                in_test_block = 1
                continue
        
        if in_test_block > 0:
            in_test_block += line.count('{')
            in_test_block -= line.count('}')
            if in_test_block <= 0:
                in_test_block = 0
            continue
        
        # Skip comments
        if stripped.startswith('//') or stripped.startswith('/*') or stripped.startswith('*'):
            continue
        
        # Check for unwrap/expect
        has_unwrap = '.unwrap()' in line or '.unwrap();' in line or '.unwrap(),' in line
        has_expect = '.expect(' in line
        
        # Check if on a line inside a #[test] fn
        in_test_fn = False
        for j in range(max(0, i-10), i):
            if re.search(r'#\[test\]', lines[j]) or re.search(r'#\[test\(', lines[j]):
                in_test_fn = True
                break
        
        if (has_unwrap or has_expect) and not in_test_fn:
            # Double-check by looking for a preceding fn signature
            fn_found = False
            for j in range(max(0, i-10), i):
                if re.search(r'fn\s+\w+\s*\(', lines[j]):
                    fn_found = True
                    break
            if not fn_found or in_test_block == 0:
                errors.append(f"{rs_file.relative_to(src.parent)}:{i+1}: {stripped[:80]}")

if errors:
    print("ERROR: Found unwrap/expect in production (non-test) code:")
    for e in errors:
        print(f"  {e}")
    sys.exit(1)
else:
    print("OK: No unwrap/expect in production code.")
    sys.exit(0)
