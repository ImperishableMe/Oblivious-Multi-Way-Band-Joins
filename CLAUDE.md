# ============== CRITICAL RULES (MUST FOLLOW) ==============

## Code Modification Rules
- **NEVER modify code with scripts** - Always edit code manually using the Edit tool. No sed, awk, perl, or any script-based modifications.

## Path Rules
- **Use absolute paths** if you can't find a file

# ============== PROJECT INFORMATION ==============

## System Architecture
- We are manipulating encrypted data obliviously outside of enclave using standard C++ STL
- Operating on non-encrypted data obliviously inside enclave using native C

## Debug Information
- Debug table dumps are saved to files in `/home/r33wei/omwj/memory_const/debug/{date}_{time}_{test}`
- Debug output goes to files, not to console