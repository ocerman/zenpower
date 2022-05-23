
# Activation

Because **ZenPower** is using same **PCI** device <br>
as **[k10temp]**, you have to disable it first.

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
    
    *If **[k10temp]** is not blacklisted, you may have to* <br>
    *manually unload k10temp after each restart.*
    
    <br>

4. Activate **ZenPower** with:
    
    ```sh
    sudo modprobe zenpower
    ```
    
<br>


<!----------------------------------------------------------------------------->

[k10temp]: https://github.com/groeck/k10temp