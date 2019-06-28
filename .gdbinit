# Print the full stack trace on python exceptions to aid debugging
set python print-stack full

# Load the mongerdb utilities
source buildscripts/gdb/monger.py

# Load the mongerdb pretty printers
source buildscripts/gdb/monger_printers.py

# Load the mongerdb lock analysis
source buildscripts/gdb/monger_lock.py
