
# Updating

<br>

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
    
<br>