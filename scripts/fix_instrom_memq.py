#!/usr/bin/env python3
"""
Fix IKAOPLL_instrom mem_q initialization and ensure case(mem_addr) has a default.

Usage:
  python3 scripts/fix_instrom_memq.py src/IKAOPLL_modules/IKAOPLL_reg.v
This creates a backup file .bak before overwriting.
"""
import sys
import re
from pathlib import Path

if len(sys.argv) != 2:
    print("Usage: python3 fix_instrom_memq.py <path/to/IKAOPLL_reg.v>")
    sys.exit(2)

p = Path(sys.argv[1])
if not p.exists():
    print("File not found:", p)
    sys.exit(3)

text = p.read_text()

# find module IKAOPLL_instrom bounds
m = re.search(r'\bmodule\s+IKAOPLL_instrom\b', text)
if not m:
    print("module IKAOPLL_instrom not found in file. Aborting.")
    sys.exit(4)
start_idx = m.start()

# Heuristic: find the end of this module by searching for the next "endmodule" after start
m_end = re.search(r'\bendmodule\b', text[start_idx:])
if not m_end:
    print("endmodule for IKAOPLL_instrom not found. Aborting.")
    sys.exit(5)
end_idx = start_idx + m_end.end()

module_text = text[start_idx:end_idx]

# 1) initialize mem_q if declaration exists
new_module_text = module_text
memq_decl_re = re.compile(r'(^\s*reg\s*\[62:0\]\s*mem_q\s*;)', flags=re.MULTILINE)
if memq_decl_re.search(module_text):
    new_module_text = memq_decl_re.sub("reg [62:0]  mem_q = 63'd0;", new_module_text, count=1)
    print("Initialized mem_q to 63'd0")
else:
    print("mem_q declaration not found or already initialized.")

# 2) find case(mem_addr) block and add default if missing
case_match = re.search(r'case\s*\(\s*mem_addr\s*\)', new_module_text)
if case_match:
    case_start = case_match.start()
    # find the first "endcase" after case_start
    endcase_match = re.search(r'\bendcase\b', new_module_text[case_start:])
    if endcase_match:
        case_block = new_module_text[case_start:case_start + endcase_match.end()]
        if 'default:' not in case_block:
            insert_pos = case_start + endcase_match.start()
            new_module_text = new_module_text[:insert_pos] + "        default: mem_q <= 63'b0;\n" + new_module_text[insert_pos:]
            print("Inserted default: mem_q <= 63'b0; before endcase")
        else:
            print("case(mem_addr) already contains a default; no insertion needed.")
    else:
        print("Could not find matching endcase for case(mem_addr). Aborting insertion.")
else:
    print("case(mem_addr) not found in module; skipping default insertion.")

# write back only if changed
if new_module_text != module_text:
    new_text = text[:start_idx] + new_module_text + text[end_idx:]
    bak = p.with_suffix(p.suffix + '.preinstromfix.bak')
    p.rename(bak)  # move original to .preinstromfix.bak
    p.write_text(new_text)
    print("Patched file written. Original backed up to:", bak)
else:
    print("No changes needed. No file overwritten.")

