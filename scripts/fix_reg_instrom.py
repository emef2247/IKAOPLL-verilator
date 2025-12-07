#!/usr/bin/env python3
"""
Fix mem_q initialization and ensure case(mem_addr) has default in IKAOPLL_instrom
Usage:
  python3 scripts/fix_reg_instrom.py src/IKAOPLL_modules/IKAOPLL_reg.v
This creates a backup: <file>.preinstromfix.bak
"""
import sys
from pathlib import Path
import re

if len(sys.argv) != 2:
    print("Usage: python3 scripts/fix_reg_instrom.py <path/to/IKAOPLL_reg.v>")
    sys.exit(2)

p = Path(sys.argv[1])
if not p.exists():
    print("File not found:", p)
    sys.exit(3)

txt = p.read_text()

# find module IKAOPLL_instrom
m = re.search(r'\bmodule\s+IKAOPLL_instrom\b', txt)
if not m:
    print("module IKAOPLL_instrom not found in file. Aborting.")
    sys.exit(4)
mod_start = m.start()

# find the 'endmodule' that closes this module (first one after mod_start)
end_match = re.search(r'\bendmodule\b', txt[mod_start:])
if not end_match:
    print("endmodule not found for IKAOPLL_instrom. Aborting.")
    sys.exit(5)
mod_end = mod_start + end_match.end()

mod_text = txt[mod_start:mod_end]

changed = False

# 1) initialize mem_q declaration if not already initialized
memq_decl_re = re.compile(r'(^\s*reg\s*\[62:0\]\s*mem_q\s*;)', flags=re.MULTILINE)
if memq_decl_re.search(mod_text):
    mod_text = memq_decl_re.sub("reg [62:0]  mem_q = 63'd0;", mod_text, count=1)
    print("Initialized mem_q to 63'd0")
    changed = True
else:
    # maybe already initialized
    if re.search(r'reg\s*\[62:0\]\s*mem_q\s*=\s*63\'d0', mod_text):
        print("mem_q already initialized")
    else:
        print("mem_q declaration pattern not found in module (unexpected).")

# 2) locate first case(mem_addr) inside module and ensure default exists before its matching endcase
case_pos = mod_text.find('case(mem_addr)')
if case_pos == -1:
    # try with spaces
    case_pos = re.search(r'case\s*\(\s*mem_addr\s*\)', mod_text)
    if case_pos:
        case_pos = case_pos.start()
    else:
        case_pos = -1

if case_pos == -1:
    print("case(mem_addr) not found in module; skipping default insertion.")
else:
    # scan from case_pos to find matching endcase (handle nested case/endcase pairs)
    i = case_pos
    depth = 0
    token_re = re.compile(r'\b(case|endcase)\b')
    for tk in token_re.finditer(mod_text, case_pos):
        tok = tk.group(1)
        if tok == 'case':
            depth += 1
        elif tok == 'endcase':
            depth -= 1
        if depth == 0:
            # tk.end() is the endcase end position
            endcase_idx = tk.start()
            break
    else:
        endcase_idx = None

    if endcase_idx is None:
        print("Could not find matching endcase for case(mem_addr); skipping insertion.")
    else:
        case_block = mod_text[case_pos:endcase_idx]
        if 'default:' in case_block:
            print("case(mem_addr) already contains a default; no insertion needed.")
        else:
            # insert default before the endcase (add proper indentation)
            insert_point = endcase_idx
            insertion = "        default: mem_q <= 63'b0;\n"
            mod_text = mod_text[:insert_point] + insertion + mod_text[insert_point:]
            print("Inserted default: mem_q <= 63'b0; before endcase")
            changed = True

# write back if changed
if changed:
    bak = p.with_suffix(p.suffix + '.preinstromfix.bak')
    p.rename(bak)
    new_txt = txt[:mod_start] + mod_text + txt[mod_end:]
    p.write_text(new_txt)
    print(f"Patched file written. Original backed up to: {bak}")
else:
    print("No changes required; file left untouched.")

