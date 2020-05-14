New effort to add profiling capabilities to GA. Reusing old code from GA

Added environment variables
GAW_FILE_PREFIX:

The prefix for each log file per node. If not defined, stdout will be used

GAW_FMT:

Two values: 0 for a CSV format and 1 for a HUMAN readable one. If it is not defined or the give value is above 1, HUMAN format is the default.

GAW_DEPTH:

Depth of the profiling (unimplemented)
