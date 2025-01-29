Monitors ifaddrs.h addrs->ifa_name for changes and executes a specified program.

```
Install: 
./configure && make && make install
```

```
Usage: ./termux-monitor-ifaddr [OPTIONS]
Options:
  -v            Enable verbose mode (prints interface changes)
  -vv           Enable very verbose mode (continuously displays current interface)
  -D            Run as a daemon
  -l,--logfile  Redirect stdin and stdout to a logfile
  -e <command>  Execute a command when interface changes
  -t <seconds>  Set throttle delay for detecting changes (default: 3 seconds)
  -h, --help    Show this help message
```
