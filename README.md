Monitors ifaddrs.h addrs->ifa_name for changes and executes a specified program.

```
Install: 
./configure && make && make install
```

```
Usage: termux-monitor-ifaddr [OPTIONS]
Options:
  -h            Show this help message
  -v            Enable verbose mode (prints interface changes)  
  -vv           Enable very verbose mode (continuously displays current interface) 
  -D            Run as a daemon
  -e <command>  Execute a command when the interface changes
  -t <seconds>  Set throttle delay for detecting changes
```
