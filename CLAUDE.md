# MiSTer Development Instructions

## Build and Deploy Commands

1. Clean and build the project:
   ```bash
   make clean && make
   ```

2. Deploy the binary to MiSTer device:
   ```bash
   sshpass -p 1 scp bin/MiSTer 192.168.1.79:/media/fat
   ```

3. Restart MiSTer on the device:
   ```bash
   killall MiSTer && ./media/fat/MiSTer
   ```

## Notes
- The MiSTer device is at IP address 192.168.1.79
- Default SSH password is "1"
- Make sure to test CEC functionality after deployment