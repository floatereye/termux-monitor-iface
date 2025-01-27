Monitors ifaddrs.h addrs->ifa_name for change, and executes specified program.

```
Install: 
./configure && make && make install
```

```
Usage: termux-monitor-ifaddr [OPTIONS]
Options:
  -h            Show this help message
  -v            Enable verbose mode (print interface and IP address)
  -vv           Enable very verbose mode (only this mode prints output)
  -D            Run as a daemon
  -e <command>  Execute a command when interface changes (detached, all parameters after -e passed)
  -t <seconds>  Set throttle delay for change detection
```
