# An example: F5 runs it in a terminal.
import os

print('Hello from the OzoneHD box!')
with open('/proc/loadavg') as f:
    print('Load over the last minute:', f.read().split()[0])
print('Free space at home: %.1f GB' % (os.statvfs(os.path.expanduser('~')).f_bavail
                                       * os.statvfs(os.path.expanduser('~')).f_frsize / 1e9))
