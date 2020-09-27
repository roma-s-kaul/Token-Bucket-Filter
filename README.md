# Token-Bucket-Filter
Emulated a traffic shaper that services packets controlled by a token bucket filter using multi-threading within a single process.

Key components:
• Achieved multithreading using POSIX pthread library; used mutex locking & condition variables for thread scheduling
• Implemented signal and interrupt handler
• Collected statistics - average wait time, packet/token drop probability, standard deviation of time spent by a packet in the system, average packet count in queues and servers
