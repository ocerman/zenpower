
# ZenPower

*Linux kernel Driver AMD Zen Family CPUs*

<br>

## Readings

- ***Temperature***
- ***Voltage*** ( SVI2 )
- ***Current*** ( SVI2 )
- ***Power*** ( SVI2 )


Make sure that your Linux kernel have support for your CPUs as Zenpower is using kernel function `amd_smn_read` to read values from SMN. A fallback method (which may or may not work!) will be used when it is detected that kernel function `amd_smn_read` lacks support for your CPU.
For AMD family 17h Model 70h (Ryzen 3000) CPUs you need kernel version 5.3.4 or newer or kernel with this patch: https://patchwork.kernel.org/patch/11043277/

<br>
<br>

## Installation

*You can install this module via `dkms`.*

<br>

### ![Badge Ubuntu]

```
sudo apt install                \
    linux-headers-$(uname -r)   \
    build-essential             \
    dkms                        \
    git

cd ~

git clone https://github.com/ocerman/zenpower.git

cd zenpower

sudo make dkms-install
```

<br>
<br>

## Activation

Because **ZenPower** is using same **PCI** device <br>
as `k10temp`, you have to disable it first.

<br>

1. Check if the device is active:

    ```sh
    lsmod | grep k10temp
    ```

    <br>

2. If active, unload it with:

    ```sh
    sudo modprobe -r k10temp
    ```
    
    <br>

3. Blacklist the device:

    ```sh
    sudo bash -c 'sudo echo -e "\n# replaced with zenpower\nblacklist k10temp" >> /etc/modprobe.d/blacklist.conf'
    ```
    
    #### Optional
    
    *If k10temp is not blacklisted, you may have to* <br>
    *manually unload k10temp after each restart.*
    
    <br>

4. Activate **ZenPower** with:
    
    ```sh
    sudo modprobe zenpower
    ```

<br>
<br>

## Monitoring

You can use the **[ZenMonitor]** or your <br>
favorite sensor monitoring software.

<br>
<br>

## Updating

1. Unload **ZenPower**:
    
    ```sh
    sudo modprobe -r zenpower
    ```
    
2. Navigate to its folder:

    ```sh
    cd ~/zenpower
    ```
    
3. Uninstall the old version:

    ```sh
    sudo make dkms-uninstall
    ```
    
4. Update the code from git
    
    ```
    git pull
    ```
    
5. Install the new version:

    ```
    sudo make dkms-install
    ```
    
6. Activate **Zenpower**:
    
    ```
    sudo modprobe zenpower
    ```

## Help needed
It would be very helpful for me for further development of Zenpower if you can share debug data from zenpower. [Read more](https://github.com/ocerman/zenpower/issues/12)

## Notes
 - Some users reported that a restart is needed after module installation
 - If you are having trouble compiling zenpower under Ubuntu 18.04 (or older) with new upstream kernel, see [#23](https://github.com/ocerman/zenpower/issues/23)
 - The meaning of raw current values from SVI2 telemetry are not standardised so the current/power readings may not be accurate on all systems (depends on the board model).



<!----------------------------------------------------------------------------->

[Badge Ubuntu]: https://img.shields.io/badge/Ubuntu-E95420?style=for-the-badge

[ZenMonitor]: https://github.com/ocerman/zenmonitor