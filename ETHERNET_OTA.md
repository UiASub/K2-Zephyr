# Ethernet OTA Setup

This project uses a direct Ethernet link for MCUboot/MCUmgr OTA updates. The
recommended direct-link network is:

- topside computer: `10.77.0.1/24`
- MCU: `10.77.0.2/24`
- MCUmgr UDP port: `1337`

Use a dedicated subnet instead of `192.168.1.0/24` so the MCU link does not
conflict with common Wi-Fi and lab networks. Do not set a default gateway on
the direct-link Ethernet interface; keep normal internet traffic on Wi-Fi or
the main LAN.

## Fedora

Install the MCUmgr CLI:

```bash
sudo dnf install golang
go install github.com/apache/mynewt-mcumgr-cli/mcumgr@latest
```

Make sure `~/go/bin` is on `PATH`, then check:

```bash
mcumgr version
```

The helper script shows a menu of Ethernet adapters, recommends likely USB
Ethernet adapters first, configures a NetworkManager profile for the selected
adapter, and then tries to ping the MCU. If the ping fails, the script warns
and continues so setup can still be prepared before the MCU is plugged in.

```bash
./tools/k2-ethernet.sh up
./tools/k2-ethernet.sh status
./tools/k2-ethernet.sh down
```

Manual setup:

```bash
nmcli connection add type ethernet ifname eth0 con-name k2-direct-eth0
nmcli connection modify k2-direct-eth0 \
  ipv4.method manual \
  ipv4.addresses 10.77.0.1/24 \
  ipv4.never-default yes \
  ipv4.gateway "" \
  ipv4.dns "" \
  ipv6.method disabled \
  connection.autoconnect no
nmcli connection up k2-direct-eth0 ifname eth0
ping -c 2 -W 1 10.77.0.2
```

Replace `eth0` with the Ethernet interface connected to the MCU. A USB Ethernet
adapter is usually the right choice when present.

## Windows

Install Go and the MCUmgr CLI:

```powershell
winget install GoLang.Go
go install github.com/apache/mynewt-mcumgr-cli/mcumgr@latest
```

Make sure `%USERPROFILE%\go\bin` is on `PATH`, then check:

```powershell
mcumgr version
```

Run PowerShell as Administrator. The helper script shows a menu of physical
Ethernet adapters, recommends likely USB Ethernet adapters first, configures
the selected adapter without a default gateway, and then tries to ping the MCU.
If the ping fails, the script warns and continues.

```powershell
.\tools\k2-ethernet.ps1 up
.\tools\k2-ethernet.ps1 status
.\tools\k2-ethernet.ps1 down
```

Manual setup:

```powershell
Get-NetAdapter
Set-NetIPInterface -InterfaceAlias "Ethernet" -AddressFamily IPv4 -Dhcp Disabled
New-NetIPAddress -InterfaceAlias "Ethernet" -IPAddress 10.77.0.1 -PrefixLength 24
Test-Connection 10.77.0.2 -Count 2
```

Replace `"Ethernet"` with the adapter connected to the MCU. Do not configure a
default gateway for this direct-link interface.

## Verify the Flashed Image

After flashing the OTA build once over USB, verify the board in this order:

1. Check the serial log for startup, static IP configuration, UDP server
   startup, and MCUboot image state.
2. Configure the direct Ethernet link with one of the scripts above.
3. Check MCUmgr reachability to `10.77.0.2:1337`.
4. Upload a signed image, mark it for test boot, reset the board, reconnect,
   and only confirm the image after the new firmware is reachable.

The current build exposes MCUmgr over UDP. Useful first checks:

```bash
mcumgr --conntype udp '--connstring=[10.77.0.2]:1337' image list
mcumgr --conntype udp '--connstring=[10.77.0.2]:1337' image upload build-h755-ota/K2-Zephyr/zephyr/zephyr.signed.bin
```
